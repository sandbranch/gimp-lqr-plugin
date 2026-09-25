# Runs inside GIMP on a Broadway display (tests/gui/start.sh): opens an
# image and the Liquid Rescale dialog.
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp

image = Gimp.Image.new(400, 300, Gimp.ImageBaseType.RGB)
layer = Gimp.Layer.new(image, 'photo', 400, 300, Gimp.ImageType.RGB_IMAGE, 100, Gimp.LayerMode.NORMAL)
image.insert_layer(layer, None, 0)
Gimp.Display.new(image)
proc = Gimp.get_pdb().lookup_procedure('plug-in-lqr')
config = proc.create_config()
config.set_property('run-mode', Gimp.RunMode.INTERACTIVE)
config.set_property('image', image)
config.set_core_object_array('drawables', [layer])
proc.run(config)
