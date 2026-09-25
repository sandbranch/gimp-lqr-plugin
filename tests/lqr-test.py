# Runs inside GIMP (tests/run.sh): shrinks a gradient image with
# plug-in-lqr in 8 bit, 16 bit and float precision and checks the result:
# the size, the value range, and that each row stays a monotonic gradient
# (removing seams removes columns; wrong pixel formats show as jumps).
import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl

W, H, NEW_W = 320, 200, 240
proc = Gimp.get_pdb().lookup_procedure('plug-in-lqr')
failures = 0

for precision in (Gimp.Precision.U8_NON_LINEAR, Gimp.Precision.U16_NON_LINEAR,
                  Gimp.Precision.FLOAT_LINEAR):
    image = Gimp.Image.new_with_precision(W, H, Gimp.ImageBaseType.RGB, precision)
    layer = Gimp.Layer.new(image, 'gradient', W, H, Gimp.ImageType.RGB_IMAGE, 100, Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    # red rises from left to right, green from top to bottom
    buf = layer.get_buffer()
    fmt = 'RGB float'
    import struct
    data = bytearray()
    for y in range(H):
        for x in range(W):
            data += struct.pack('fff', x / (W - 1), y / (H - 1), 0.5)
    buf.set(Gegl.Rectangle.new(0, 0, W, H), fmt, bytes(data))
    buf.flush()
    layer.update(0, 0, W, H)

    config = proc.create_config()
    config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    config.set_property('image', image)
    config.set_core_object_array('drawables', [layer])
    config.set_property('width', NEW_W)
    config.set_property('height', H)
    result = proc.run(config)

    layer = image.get_layers()[0]
    w, h = layer.get_width(), layer.get_height()
    buf = layer.get_buffer()
    raw = buf.get(Gegl.Rectangle.new(0, 0, w, h), 1.0, fmt, Gegl.AbyssPolicy.NONE)
    vals = struct.unpack('%df' % (w * h * 3), raw)
    problems = []
    if result.index(0) != Gimp.PDBStatusType.SUCCESS:
        problems.append('status %s' % result.index(0))
    if (w, h) != (NEW_W, H):
        problems.append('size %dx%d' % (w, h))
    if min(vals) < -0.01 or max(vals) > 1.01:
        problems.append('values out of range %.3f..%.3f' % (min(vals), max(vals)))
    for y in range(0, h, 20):
        row = [vals[(y * w + x) * 3] for x in range(w)]
        if any(b < a - 0.02 for a, b in zip(row, row[1:])):
            problems.append('row %d not monotonic' % y)
            break
    green = [vals[(y * w + w // 2) * 3 + 1] for y in range(h)]
    if abs(green[-1] - 1.0) > 0.02 or abs(green[0]) > 0.02:
        problems.append('green column %.3f..%.3f' % (green[0], green[-1]))
    print('LQR', precision.value_nick, 'ok' if not problems else 'FAIL ' + '; '.join(problems))
    failures += bool(problems)
    image.delete()

print('LQR failures:', failures)
