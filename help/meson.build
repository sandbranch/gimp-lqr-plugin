# Install help files next to the plug-in, where it registers them:
# help/<language>/gimp-help.xml, with the style sheets in help/css
helpdir = bindir / 'help'

message('Help installation path: ' + helpdir)

foreach lang : ['en', 'it', 'ru', 'es_ES', 'css']
  install_subdir(lang, install_dir : helpdir)
endforeach
