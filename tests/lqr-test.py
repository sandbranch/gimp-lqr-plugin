# Runs inside GIMP (tests/run.sh), without a window: calls plug-in-lqr
# non-interactively on generated images and checks the results numerically.
# Prints "LQR PASS <case>" or "LQR FAIL <case>: <why>" for each case and a
# summary line "LQR failures: <n>" at the end.
#
# Images are made of gradients (seam carving must keep each row and column
# in order: wrong pixel formats or offsets show as jumps) and of noise
# (every pixel matters, so the masks decide which pixels go).
import os
import random
import re
import struct
import sys
import traceback

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl

PDB = Gimp.get_pdb()
PROC = PDB.lookup_procedure('plug-in-lqr')
FMT = "R'G'B'A float"

U8 = Gimp.Precision.U8_NON_LINEAR
U16 = Gimp.Precision.U16_NON_LINEAR
FLOAT = Gimp.Precision.FLOAT_LINEAR

cases = []
failures = []


def case(func):
    cases.append(func)
    return func


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


# ---------------------------------------------------------------- helpers

def new_image(w, h, base=Gimp.ImageBaseType.RGB, precision=U8, alpha=False,
              pixel=None, name='photo'):
    """An image with one layer of w x h, filled by pixel(x, y) -> (r, g, b, a)."""
    image = Gimp.Image.new_with_precision(w, h, base, precision)
    if base == Gimp.ImageBaseType.RGB:
        itype = Gimp.ImageType.RGBA_IMAGE if alpha else Gimp.ImageType.RGB_IMAGE
    else:
        itype = Gimp.ImageType.GRAYA_IMAGE if alpha else Gimp.ImageType.GRAY_IMAGE
    layer = Gimp.Layer.new(image, name, w, h, itype, 100, Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    if pixel:
        put(layer, pixel)
    return image, layer


def new_layer(image, name, w, h, x=0, y=0, pixel=None):
    """An RGBA layer at (x, y), transparent unless pixel(x, y) is given."""
    layer = Gimp.Layer.new(image, name, w, h, Gimp.ImageType.RGBA_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    layer.set_offsets(x, y)
    layer.fill(Gimp.FillType.TRANSPARENT)
    if pixel:
        put(layer, pixel)
    return layer


def put(drawable, pixel):
    w, h = drawable.get_width(), drawable.get_height()
    data = bytearray()
    for y in range(h):
        for x in range(w):
            data += struct.pack('ffff', *pixel(x, y))
    buf = drawable.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, w, h), FMT, bytes(data))
    buf.flush()
    drawable.update(0, 0, w, h)


def get(drawable):
    """The pixels as rows of (r, g, b, a) floats."""
    w, h = drawable.get_width(), drawable.get_height()
    buf = drawable.get_buffer()
    raw = buf.get(Gegl.Rectangle.new(0, 0, w, h), 1.0, FMT, Gegl.AbyssPolicy.NONE)
    vals = struct.unpack('%df' % (w * h * 4), raw)
    return [[vals[(y * w + x) * 4:(y * w + x) * 4 + 4] for x in range(w)]
            for y in range(h)]


def gradient(w, h):
    """Red rises from left to right, green from top to bottom."""
    return lambda x, y: (x / max(w - 1, 1), y / max(h - 1, 1), 0.5, 1.0)


def noise(seed, lo=0.2, hi=0.8):
    rnd = random.Random(seed)
    table = {}

    def pixel(x, y):
        if (x, y) not in table:
            table[(x, y)] = (rnd.uniform(lo, hi), rnd.uniform(lo, hi),
                             rnd.uniform(lo, hi), 1.0)
        return table[(x, y)]
    return pixel


def lqr(image, drawables, **args):
    """Runs plug-in-lqr non-interactively; returns the PDB status."""
    config = PROC.create_config()
    config.set_property('run-mode', args.pop('run_mode', Gimp.RunMode.NONINTERACTIVE))
    config.set_property('image', image)
    config.set_core_object_array('drawables', drawables)
    for key, value in args.items():
        config.set_property(key, value)
    result = PROC.run(config)
    return result.index(0)


def ok(status):
    check(status == Gimp.PDBStatusType.SUCCESS, 'status %s' % status.value_nick)


def size(item):
    return item.get_width(), item.get_height()


def monotonic_rows(pix, channel=0, tol=0.02):
    for y, row in enumerate(pix):
        vals = [p[channel] for p in row]
        for a, b in zip(vals, vals[1:]):
            if b < a - tol:
                return 'row %d not monotonic' % y
    return None


def monotonic_columns(pix, channel=1, tol=0.02):
    for x in range(len(pix[0])):
        vals = [row[x][channel] for row in pix]
        for a, b in zip(vals, vals[1:]):
            if b < a - tol:
                return 'column %d not monotonic' % x
    return None


def in_range(pix):
    flat = [c for row in pix for p in row for c in p]
    return min(flat) >= -0.01 and max(flat) <= 1.01


def image_layer(image, name):
    for layer in image.get_layers():
        if layer.get_name() == name:
            return layer
    return None


def check_gradient(layer, gray=False, alpha=1.0, rows=True, columns=True):
    pix = get(layer)
    check(in_range(pix), 'values out of range')
    if rows:
        problem = monotonic_rows(pix, 0)
        check(problem is None, problem)
    if columns and not gray:
        problem = monotonic_columns(pix, 1)
        check(problem is None, problem)
    if alpha is not None:
        check(all(abs(p[3] - alpha) < 0.01 for row in pix for p in row),
              'alpha changed')
    return pix


# ------------------------------------------- precisions and image types

TYPES = [('rgb', Gimp.ImageBaseType.RGB, False),
         ('rgba', Gimp.ImageBaseType.RGB, True),
         ('gray', Gimp.ImageBaseType.GRAY, False),
         ('graya', Gimp.ImageBaseType.GRAY, True)]


def make_type_case(prec, tname, base, alpha):
    def run():
        W, H = 64, 40
        gray = base == Gimp.ImageBaseType.GRAY
        # gray images keep one channel: the horizontal gradient
        pixel = ((lambda x, y: (x / (W - 1),) * 3 + (1.0,)) if gray
                 else gradient(W, H))
        image, layer = new_image(W, H, base, prec, alpha, pixel)
        ok(lqr(image, [layer], width=48, height=H))
        layer = image.get_layers()[0]
        check(size(layer) == (48, H), 'size %dx%d' % size(layer))
        check(image.get_precision() == prec, 'precision changed')
        check(layer.has_alpha() == alpha, 'alpha channel changed')
        check_gradient(layer, gray=gray)
        image.delete()
    run.__name__ = 'shrink-width-%s-%s' % (prec.value_nick, tname)
    return run


for _prec in (U8, U16, FLOAT):
    for _t in TYPES:
        case(make_type_case(_prec, *_t))


# ------------------------------------------------- shrink and enlarge

def resize_case(name, W, H, NW, NH, strict=True, **args):
    # strict: rows and columns stay in order; this does not hold when a
    # second pass carves the result of the first one down to 1 pixel
    def run():
        image, layer = new_image(W, H, pixel=gradient(W, H))
        ok(lqr(image, [layer], width=NW, height=NH, **args))
        layer = image.get_layers()[0]
        check(size(layer) == (NW, NH), 'size %dx%d' % size(layer))
        check(size(image) == (NW, NH), 'canvas %dx%d' % size(image))
        check_gradient(layer, rows=strict and NW > 1, columns=strict and NH > 1)
        image.delete()
    run.__name__ = name
    return case(run)


resize_case('shrink-height', 60, 50, 60, 30)
resize_case('shrink-both', 60, 50, 40, 30)
resize_case('shrink-both-vertical-first', 60, 50, 40, 30, res_order=1)
resize_case('enlarge-width', 60, 40, 85, 40)
resize_case('enlarge-height', 60, 40, 60, 55)
resize_case('enlarge-both', 60, 40, 80, 50)
resize_case('enlarge-width-2.5x', 40, 20, 100, 20)
resize_case('enlarge-height-3x', 20, 20, 20, 60)
resize_case('enlarge-small-steps', 40, 20, 90, 20, enl_step=110.0)
resize_case('shrink-to-width-1', 40, 20, 1, 20)
resize_case('shrink-to-height-1', 40, 20, 40, 1)
resize_case('shrink-width-enlarge-height', 60, 40, 40, 55)
resize_case('1x1-same', 1, 1, 1, 1)
resize_case('2x2-enlarge', 2, 2, 5, 4)
# liblqr cannot carve an image that is 1 pixel wide or high (it reads
# outside its buffers), so the order of the two passes is swapped where
# that avoids such a pass
resize_case('shrink-to-width-1-and-height', 40, 20, 1, 10, strict=False)
resize_case('shrink-to-height-1-and-width', 40, 20, 10, 1, strict=False,
            res_order=1)


def refused_case(name, W, H, NW, NH, **args):
    # and where it cannot be avoided, the call is refused
    def run():
        image, layer = new_image(W, H, pixel=gradient(W, H))
        before = get(layer)
        status = lqr(image, [layer], width=NW, height=NH, **args)
        check(status == Gimp.PDBStatusType.EXECUTION_ERROR,
              'status %s' % status.value_nick)
        layer = image.get_layers()[0]
        check(size(layer) == (W, H), 'resized to %dx%d' % size(layer))
        check(get(layer) == before, 'pixels changed')
        image.delete()
    run.__name__ = name
    return case(run)


refused_case('refuse-shrink-to-1x1', 40, 20, 1, 1)
refused_case('refuse-1px-column-enlarge', 1, 30, 4, 30)
refused_case('refuse-1px-row-enlarge', 30, 1, 30, 3)
refused_case('refuse-1px-column-shrink-height', 1, 30, 1, 20)
refused_case('refuse-1x1-enlarge', 1, 1, 3, 2)


@case
def same_size():
    W, H = 50, 30
    image, layer = new_image(W, H, pixel=noise(1))
    before = get(layer)
    ok(lqr(image, [layer], width=W, height=H))
    layer = image.get_layers()[0]
    check(size(layer) == (W, H), 'size %dx%d' % size(layer))
    after = get(layer)
    diff = max(abs(a - b) for ra, rb in zip(before, after)
               for pa, pb in zip(ra, rb) for a, b in zip(pa, pb))
    check(diff < 1e-6, 'pixels changed by %g' % diff)
    image.delete()


# ------------------------------------------------ preservation and discard

# A flat square (no energy inside, so seams go through it first) on noise
SQ = (40, 20, 30, 30)   # x, y, w, h in layer coordinates


def square_scene(W=120, H=70, seed=2):
    bg = noise(seed)
    x0, y0, sw, sh = SQ

    def pixel(x, y):
        if x0 <= x < x0 + sw and y0 <= y < y0 + sh:
            return (1.0, 0.0, 0.0, 1.0)
        return bg(x, y)
    return W, H, pixel


def square_widths(layer):
    """The width of the red square in each of its rows."""
    widths = []
    for row in get(layer):
        n = sum(1 for p in row if p[0] > 0.95 and p[1] < 0.05 and p[2] < 0.05)
        if n:
            widths.append(n)
    return widths


def opaque(x, y):
    return (1.0, 1.0, 1.0, 1.0)


@case
def square_shrinks_without_mask():
    # the control for the preservation cases: without a mask the square goes
    W, H, pixel = square_scene()
    image, layer = new_image(W, H, pixel=pixel)
    ok(lqr(image, [layer], width=W - 40, height=H))
    widths = square_widths(image.get_layers()[0])
    check(widths and max(widths) < SQ[2] - 10,
          'square kept without a mask: widths %s' % sorted(set(widths)))
    image.delete()


def check_square_kept(layer):
    widths = square_widths(layer)
    check(len(widths) == SQ[3], 'square rows %d' % len(widths))
    check(min(widths) == SQ[2] and max(widths) == SQ[2],
          'square widths %s' % sorted(set(widths)))


@case
def preserve_mask():
    W, H, pixel = square_scene()
    image, layer = new_image(W, H, pixel=pixel)
    x0, y0, sw, sh = SQ
    mask = new_layer(image, 'keep', sw, sh, x0, y0, opaque)
    ok(lqr(image, [layer], width=W - 40, height=H, pres_layer=mask))
    check(size(layer) == (W - 40, H), 'size %dx%d' % size(layer))
    check_square_kept(layer)
    # the mask is resized along (resize_aux_layers is on by default)
    check(size(mask) == (W - 40, H), 'mask size %dx%d' % size(mask))
    image.delete()


@case
def preserve_mask_by_name():
    W, H, pixel = square_scene()
    image, layer = new_image(W, H, pixel=pixel)
    x0, y0, sw, sh = SQ
    new_layer(image, 'keep', sw, sh, x0, y0, opaque)
    ok(lqr(image, [layer], width=W - 40, height=H, pres_layer_name='keep'))
    check_square_kept(image_layer(image, 'photo'))
    image.delete()


@case
def preserve_mask_offset_layer():
    # the layer is not at the image origin, and the mask is smaller than
    # the layer, at its own offset
    W, H, pixel = square_scene()
    image = Gimp.Image.new(W + 30, H + 20, Gimp.ImageBaseType.RGB)
    layer = Gimp.Layer.new(image, 'photo', W, H, Gimp.ImageType.RGB_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    layer.set_offsets(20, 10)
    put(layer, pixel)
    x0, y0, sw, sh = SQ
    new_layer(image, 'keep', sw + 10, sh + 10, 20 + x0 - 5, 10 + y0 - 5, opaque)
    mask = image_layer(image, 'keep')
    ok(lqr(image, [layer], width=W - 40, height=H, pres_layer=mask,
           resize_canvas=False))
    check(size(layer) == (W - 40, H), 'size %dx%d' % size(layer))
    check(size(image) == (W + 30, H + 20), 'canvas %dx%d' % size(image))
    check(tuple(layer.get_offsets()[1:]) == (20, 10),
          'offsets %s' % (layer.get_offsets()[1:],))
    check_square_kept(layer)
    image.delete()


@case
def preserve_mask_not_resized():
    W, H, pixel = square_scene()
    image, layer = new_image(W, H, pixel=pixel)
    x0, y0, sw, sh = SQ
    mask = new_layer(image, 'keep', sw, sh, x0, y0, opaque)
    ok(lqr(image, [layer], width=W - 40, height=H, pres_layer=mask,
           resize_aux_layers=False))
    check_square_kept(layer)
    check(size(mask) == (sw, sh), 'mask resized to %dx%d' % size(mask))
    image.delete()


# a band of noise as strong as the rest, told apart by its blue channel
# (0.05 or 0.95; elsewhere 0.2 to 0.8), which a discard mask removes first
BAND = (70, 20)   # x, width


def band_scene(W=120, H=50):
    bg = noise(3)
    rnd = random.Random(4)
    bx, bw = BAND
    blue = dict(((x, y), rnd.choice((0.05, 0.95)))
                for x in range(bx, bx + bw) for y in range(H))

    def pixel(x, y):
        r, g, b, a = bg(x, y)
        if bx <= x < bx + bw:
            b = blue[(x, y)]
        return (r, g, b, a)
    return W, H, pixel


def blue_fraction(layer):
    pix = get(layer)
    n = sum(1 for row in pix for p in row if p[2] > 0.9 or p[2] < 0.1)
    return n / float(BAND[1] * len(pix))


@case
def discard_mask():
    W, H, pixel = band_scene()
    image, layer = new_image(W, H, pixel=pixel)
    bx, bw = BAND
    mask = new_layer(image, 'drop', bw, H, bx, 0, opaque)
    ok(lqr(image, [layer], width=W - bw, height=H, disc_layer=mask))
    check(size(layer) == (W - bw, H), 'size %dx%d' % size(layer))
    left = blue_fraction(layer)
    check(left < 0.05, '%.0f%% of the band left' % (100 * left))
    image.delete()


@case
def discard_control_without_mask():
    W, H, pixel = band_scene()
    image, layer = new_image(W, H, pixel=pixel)
    ok(lqr(image, [layer], width=W - BAND[1], height=H))
    left = blue_fraction(layer)
    check(left > 0.5, 'only %.0f%% of the band left' % (100 * left))
    image.delete()


@case
def discard_mask_by_name():
    W, H, pixel = band_scene()
    image, layer = new_image(W, H, pixel=pixel)
    bx, bw = BAND
    new_layer(image, 'drop', bw, H, bx, 0, opaque)
    ok(lqr(image, [layer], width=W - bw, height=H, disc_layer_name='drop'))
    left = blue_fraction(image_layer(image, 'photo'))
    check(left < 0.05, '%.0f%% of the band left' % (100 * left))
    image.delete()


@case
def discard_ignored_when_enlarging():
    # enlarging with no_disc_on_enlarge (the default) ignores the discard
    # mask; without it the call must still work
    W, H, pixel = band_scene()
    for flag in (True, False):
        image, layer = new_image(W, H, pixel=pixel)
        mask = new_layer(image, 'drop', BAND[1], H, BAND[0], 0, opaque)
        ok(lqr(image, [layer], width=W + 20, height=H, disc_layer=mask,
               no_disc_on_enlarge=flag))
        check(size(layer) == (W + 20, H), 'size %dx%d' % size(layer))
        image.delete()


# ----------------------------------------------- the numeric arguments

def noise_result(W=60, H=40, **args):
    image, layer = new_image(W, H, pixel=noise(5))
    ok(lqr(image, [layer], **args))
    pix = get(image.get_layers()[0])
    image.delete()
    return pix


@case
def rigidity_has_effect():
    a = noise_result(width=40, height=40, rigidity=0.0)
    b = noise_result(width=40, height=40, rigidity=500.0)
    check(a != b, 'rigidity 500 gives the same result as 0')


@case
def rigidity_mask():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=noise(5))
    rig = new_layer(image, 'rigid', W, H, 0, 0, opaque)
    ok(lqr(image, [layer], width=40, height=H, rigidity=500.0,
           rigidity_mask_layer=rig))
    check(size(layer) == (40, H), 'size %dx%d' % size(layer))
    check(size(rig) == (40, H), 'rigidity mask size %dx%d' % size(rig))
    image.delete()


@case
def enl_step_has_effect():
    a = noise_result(width=110, height=40, enl_step=110.0)
    b = noise_result(width=110, height=40, enl_step=195.0)
    check(len(a[0]) == 110 and len(b[0]) == 110, 'size')
    check(a != b, 'enl_step 110 gives the same result as 195')


@case
def delta_x_has_effect():
    a = noise_result(width=40, height=40, delta_x=0)
    b = noise_result(width=40, height=40, delta_x=1)
    check(a != b, 'delta_x 0 gives the same result as 1')


@case
def energy_functions():
    results = []
    for nrg in range(7):
        results.append(noise_result(width=40, height=40, nrg_func=nrg))
    distinct = len(set(str(r) for r in results))
    check(distinct >= 5, 'only %d distinct results of 7 energy functions' % distinct)


@case
def energy_function_out_of_range():
    image, layer = new_image(20, 20, pixel=noise(1))
    config = PROC.create_config()
    spec = config.find_property('nrg-func')
    check(spec.maximum == 6, 'nrg_func accepts up to %d' % spec.maximum)
    image.delete()


@case
def argument_ranges_match_the_dialog():
    config = PROC.create_config()
    problems = []
    for name, lo, hi, default in (('enl-step', 100.1, 200.0, 150.0),
                                  ('rigidity', 0.0, 1000.0, 0.0),
                                  ('mask-behavior', 0, 1, 0),
                                  ('scaleback-mode', 0, 3, 0),
                                  ('nrg-func', 0, 6, 2)):
        spec = config.find_property(name)
        got = (spec.minimum, spec.maximum, spec.get_default_value())
        if (abs(got[0] - lo) > 1e-6 or abs(got[1] - hi) > 1e-6
                or abs(got[2] - default) > 1e-6):
            problems.append('%s %s..%s (%s)' % ((name,) + got))
    check(not problems, ', '.join(problems))


# ------------------------------------------------------------ output

@case
def output_new_layer():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    before = get(layer)
    ok(lqr(image, [layer], width=45, height=H, output_target=1))
    check(len(image.get_layers()) == 2, '%d layers' % len(image.get_layers()))
    new = image_layer(image, 'photo LqR')
    check(new is not None, 'no layer "photo LqR"')
    check(size(new) == (45, H), 'new layer size %dx%d' % size(new))
    check(new.get_visible(), 'new layer hidden')
    check_gradient(new)
    check(size(layer) == (W, H), 'original resized to %dx%d' % size(layer))
    check(get(layer) == before, 'original layer changed')
    image.delete()


@case
def output_new_image():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    before_images = set(i.get_id() for i in Gimp.get_images())
    ok(lqr(image, [layer], width=45, height=30, output_target=2))
    new = [i for i in Gimp.get_images() if i.get_id() not in before_images]
    check(len(new) == 1, '%d new images' % len(new))
    new = new[0]
    check(size(new) == (45, 30), 'new image %dx%d' % size(new))
    check(size(new.get_layers()[0]) == (45, 30), 'new layer size')
    check_gradient(new.get_layers()[0])
    check(size(image) == (W, H) and size(layer) == (W, H), 'original resized')
    new.delete()
    image.delete()


@case
def output_new_image_with_mask():
    # the layer and its mask are not at the origin of the image
    W, H, pixel = square_scene()
    image = Gimp.Image.new(W + 20, H + 10, Gimp.ImageBaseType.RGB)
    layer = Gimp.Layer.new(image, 'photo', W, H, Gimp.ImageType.RGB_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    layer.set_offsets(10, 5)
    put(layer, pixel)
    x0, y0, sw, sh = SQ
    mask = new_layer(image, 'keep', sw, sh, 10 + x0, 5 + y0, opaque)
    before_images = set(i.get_id() for i in Gimp.get_images())
    ok(lqr(image, [layer], width=W - 40, height=H, pres_layer=mask,
           output_target=2))
    new = [i for i in Gimp.get_images() if i.get_id() not in before_images]
    check(len(new) == 1, '%d new images' % len(new))
    new = new[0]
    photo = image_layer(new, 'photo')
    check(photo is not None, 'no layer "photo" in the new image')
    check_square_kept(photo)
    check(size(mask) == (sw, sh), 'mask of the original image resized')
    new.delete()
    image.delete()


@case
def resize_canvas_off():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    ok(lqr(image, [layer], width=45, height=50, resize_canvas=False))
    check(size(layer) == (45, 50), 'size %dx%d' % size(layer))
    check(size(image) == (W, H), 'canvas %dx%d' % size(image))
    image.delete()


@case
def resize_canvas_offset_layer():
    W, H = 60, 40
    image = Gimp.Image.new(100, 80, Gimp.ImageBaseType.RGB)
    layer = Gimp.Layer.new(image, 'photo', W, H, Gimp.ImageType.RGB_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    layer.set_offsets(15, 12)
    put(layer, gradient(W, H))
    other = new_layer(image, 'other', 10, 10, 30, 30)
    ok(lqr(image, [layer], width=45, height=30, resize_canvas=True))
    check(size(image) == (45, 30), 'canvas %dx%d' % size(image))
    check(tuple(layer.get_offsets()[1:]) == (0, 0),
          'offsets %s' % (layer.get_offsets()[1:],))
    # other layers move with the canvas
    check(tuple(other.get_offsets()[1:]) == (15, 18),
          'other layer at %s' % (other.get_offsets()[1:],))
    check_gradient(layer)
    image.delete()


@case
def seams_output():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=noise(6))
    ok(lqr(image, [layer], width=45, height=H, seams=True, output_target=1))
    seams = [l for l in image.get_layers() if l.get_name().endswith('seam map')]
    seams = seams[0] if seams else None
    check(seams is not None,
          'no seam map; layers %s' % [l.get_name() for l in image.get_layers()])
    check(size(seams) == (W, H), 'seam map %dx%d' % size(seams))
    pix = get(seams)
    marked = [p for row in pix for p in row if p[3] > 0]
    # 15 seams of H pixels each
    check(len(marked) == 15 * H, '%d seam pixels' % len(marked))
    # the first seams are drawn in the first colour (default red, 1 1 0 ->
    # yellow in the plug-in defaults), the last ones in the second one
    reds = [p[0] for p in marked]
    check(min(reds) > 0.1 and max(reds) > 0.9, 'seam colours %.2f..%.2f'
          % (min(reds), max(reds)))
    image.delete()


@case
def seams_output_gray():
    # the seam map is in colour: a gray image is converted to RGB
    W, H = 40, 30
    image, layer = new_image(W, H, Gimp.ImageBaseType.GRAY, pixel=noise(7))
    ok(lqr(image, [layer], width=30, height=H, seams=True))
    check(image.get_base_type() == Gimp.ImageBaseType.RGB, 'still gray')
    check(any(l.get_name().endswith('seam map') for l in image.get_layers()),
          'no seam map')
    image.delete()


@case
def scaleback_lqr():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    ok(lqr(image, [layer], width=40, height=30, scaleback=True, scaleback_mode=0))
    check(size(layer) == (W, H), 'size %dx%d' % size(layer))
    check(size(image) == (W, H), 'canvas %dx%d' % size(image))
    check_gradient(layer)
    image.delete()


def scaleback_case(mode, expect):
    def run():
        W, H = 60, 40
        image, layer = new_image(W, H, pixel=gradient(W, H))
        ok(lqr(image, [layer], width=40, height=30, scaleback=True,
               scaleback_mode=mode))
        check(size(layer) == expect, 'size %dx%d' % size(layer))
        check(size(image) == expect, 'canvas %dx%d' % size(image))
        check(tuple(layer.get_offsets()[1:]) == (0, 0),
              'offsets %s' % (layer.get_offsets()[1:],))
        image.delete()
    run.__name__ = 'scaleback-mode-%d' % mode
    return case(run)


scaleback_case(1, (60, 40))
scaleback_case(2, (60, 45))
scaleback_case(3, (53, 40))


@case
def layer_mask_apply_and_discard():
    W, H = 40, 30
    for behavior, expect_alpha in ((0, 0.0), (1, 1.0)):
        image, layer = new_image(W, H, pixel=gradient(W, H), alpha=True)
        mask = layer.create_mask(Gimp.AddMaskType.BLACK)
        layer.add_mask(mask)
        ok(lqr(image, [layer], width=30, height=H, mask_behavior=behavior))
        check(layer.get_mask() is None, 'mask kept')
        pix = get(layer)
        alphas = set(round(p[3], 2) for row in pix for p in row)
        check(alphas == {expect_alpha}, 'mode %d alpha %s' % (behavior, alphas))
        image.delete()


@case
def every_argument():
    W, H, pixel = square_scene()
    image, layer = new_image(W, H, pixel=pixel)
    x0, y0, sw, sh = SQ
    keep = new_layer(image, 'keep', sw, sh, x0, y0, opaque)
    drop = new_layer(image, 'drop', 5, H, 5, 0, opaque)
    rig = new_layer(image, 'rigid', 10, H, 100, 0, opaque)
    ok(lqr(image, [layer], width=W - 30, height=H - 5,
           pres_layer=keep, pres_coeff=2000,
           disc_layer=drop, disc_coeff=1500,
           rigidity=2.0, rigidity_mask_layer=rig,
           delta_x=2, enl_step=150.0,
           resize_aux_layers=True, resize_canvas=True,
           output_target=0, seams=False, nrg_func=0, res_order=1,
           mask_behavior=0, scaleback=False, scaleback_mode=0,
           no_disc_on_enlarge=True,
           pres_layer_name='', disc_layer_name='', rigmask_layer_name='',
           selected_layer_name=''))
    check(size(layer) == (W - 30, H - 5), 'size %dx%d' % size(layer))
    for aux in (keep, drop, rig):
        check(size(aux) == (W - 30, H - 5),
              '%s size %dx%d' % ((aux.get_name(),) + size(aux)))
    check(size(image) == (W - 30, H - 5), 'canvas %dx%d' % size(image))
    image.delete()


@case
def selected_layer_name():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    other = new_layer(image, 'other', W, H, 0, 0, opaque)
    ok(lqr(image, [other], width=45, height=H, selected_layer_name='photo'))
    check(size(layer) == (45, H), 'named layer size %dx%d' % size(layer))
    check(size(other) == (W, H), 'passed layer resized to %dx%d' % size(other))
    image.delete()


# --------------------------------------------- drawables and image types

@case
def two_drawables():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    other = new_layer(image, 'other', W, H, 0, 0, opaque)
    ok(lqr(image, [layer, other], width=45, height=H))
    check(size(layer) == (45, H), 'first drawable %dx%d' % size(layer))
    image.delete()


@case
def no_drawable():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    image.set_selected_layers([layer])
    status = lqr(image, [], width=45, height=H)
    # either the selected layer is used, or the call is refused cleanly
    check(status in (Gimp.PDBStatusType.SUCCESS,
                     Gimp.PDBStatusType.CALLING_ERROR,
                     Gimp.PDBStatusType.EXECUTION_ERROR),
          'status %s' % status.value_nick)
    if status == Gimp.PDBStatusType.SUCCESS:
        check(size(layer) == (45, H), 'size %dx%d' % size(layer))
    image.delete()


@case
def channel_drawable():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    image.set_selected_layers([layer])
    channel = Gimp.Channel.new(image, 'chan', W, H, 50, Gegl.Color.new('black'))
    image.insert_channel(channel, None, 0)
    ok(lqr(image, [channel], width=45, height=H))
    check(size(layer) == (45, H), 'selected layer %dx%d' % size(layer))
    image.delete()


@case
def indexed_image_refused():
    image, layer = new_image(30, 20, pixel=noise(8))
    image.convert_indexed(Gimp.ConvertDitherType.NONE,
                          Gimp.ConvertPaletteType.GENERATE, 16, False, False, '')
    status = lqr(image, [layer], width=20, height=20)
    check(status != Gimp.PDBStatusType.SUCCESS, 'indexed image accepted')
    check(size(layer) == (30, 20), 'indexed layer resized')
    image.delete()


@case
def group_layer_refused():
    image, layer = new_image(30, 20, pixel=noise(8))
    group = Gimp.GroupLayer.new(image, 'group')
    image.insert_layer(group, None, 0)
    image.reorder_item(layer, group, 0)
    status = lqr(image, [group], width=20, height=20)
    check(status != Gimp.PDBStatusType.SUCCESS, 'group layer accepted')
    image.delete()


@case
def selection_is_kept_aside():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    image.select_rectangle(Gimp.ChannelOps.REPLACE, 10, 10, 20, 20)
    ok(lqr(image, [layer], width=45, height=H))
    check(size(layer) == (45, H), 'size %dx%d' % size(layer))
    check_gradient(layer)
    image.delete()


@case
def alpha_lock_restored():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H), alpha=True)
    layer.set_lock_alpha(True)
    ok(lqr(image, [layer], width=45, height=H))
    check(layer.get_lock_alpha(), 'alpha lock lost')
    check(size(layer) == (45, H), 'size %dx%d' % size(layer))
    image.delete()


@case
def mask_from_another_image_ignored():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    image2, foreign = new_image(W, H, pixel=noise(9), name='foreign')
    status = lqr(image, [layer], width=45, height=H, pres_layer=foreign)
    check(size(foreign) == (W, H), 'layer of the other image resized')
    check(status != Gimp.PDBStatusType.SUCCESS or size(layer) == (45, H),
          'size %dx%d' % size(layer))
    image.delete()
    image2.delete()


@case
def repeat_uses_last_values():
    # "Repeat" runs with the last values, which GIMP saves after each
    # interactive run in plug-in-settings of the profile (the test profile)
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    settings = os.path.join(Gimp.directory(), 'plug-in-settings')
    os.makedirs(settings, exist_ok=True)
    last = os.path.join(settings, 'GimpProcedureConfigRun-plug-in-lqr.last')
    with open(last, 'w') as f:
        f.write('(width 33)\n(height 25)\n(resize-canvas no)\n')
    try:
        ok(lqr(image, [layer], run_mode=Gimp.RunMode.WITH_LAST_VALS))
    finally:
        os.remove(last)
    check(size(layer) == (33, 25), 'size %dx%d' % size(layer))
    check(size(image) == (W, H), 'canvas %dx%d' % size(image))
    image.delete()


@case
def undo_works():
    W, H = 60, 40
    image, layer = new_image(W, H, pixel=gradient(W, H))
    ok(lqr(image, [layer], width=45, height=30))
    # an open undo group is closed by GIMP with a warning in the log, which
    # tests/run.sh looks for; here the image must at least still be usable
    check(image.undo_is_enabled(), 'undo disabled')
    image.delete()


# ------------------------------------------------------------------- run

# LQR_ONLY: a regular expression for the cases to run (tests/run.sh passes
# it on from its environment)
only = re.compile(os.environ.get('LQR_ONLY') or '.')
for func in cases:
    name = func.__name__.replace('_', '-')
    if not only.search(name):
        continue
    try:
        func()
        print('LQR PASS', name)
    except Fail as e:
        failures.append(name)
        print('LQR FAIL %s: %s' % (name, e))
    except Exception as e:
        failures.append(name)
        print('LQR FAIL %s: %s: %s' % (name, type(e).__name__, e))
        traceback.print_exc(file=sys.stdout)
    sys.stdout.flush()

print('LQR failures:', len(failures))
