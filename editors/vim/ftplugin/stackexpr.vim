" Settings for Stackexpr source files.
if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

setlocal comments=://
setlocal commentstring=//\ %s
setlocal iskeyword=@,48-57,_,.
" Continue comments on new lines, and wrap long comments.
setlocal formatoptions-=t formatoptions+=croql

let b:undo_ftplugin = "setlocal comments< commentstring< iskeyword< formatoptions<"
