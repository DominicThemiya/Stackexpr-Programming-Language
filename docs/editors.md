# Editor support

## Vim and Neovim

`editors/vim/` contains syntax highlighting for Stackexpr, filetype detection for `.sxp` files, and settings for comments (`gc`-style commenting plugins and `gq` use `//`).

### Installing

Copy it into your Vim directory:

```sh
make install-vim                                 # Vim: ~/.vim
make install-vim VIM_DIR=~/.config/nvim          # Neovim
```

Or point your plugin manager at the folder, so it updates with the repository. For example, with vim-plug:

```vim
Plug 'DominicThemiya/Stackexpr-Programming-Language', { 'rtp': 'editors/vim' }
```

or, without a plugin manager, add the folder to the runtime path in your `vimrc`:

```vim
set runtimepath+=/path/to/Stackexpr-Programming-Language/editors/vim
```

Syntax highlighting needs `syntax on` and `filetype plugin on`, which most configurations already have.

### What is highlighted

| Element | Examples | Highlight group |
| --- | --- | --- |
| Comments | `// ...`, with `TODO`, `FIXME`, `XXX`, `NOTE` | `Comment`, `Todo` |
| Definitions and their names | `stackexpr square`, `interrupt on_timer`, `struct Point`, `const N` | `Define`, `Structure`, `StorageClass`, `Function` |
| Control flow | `if else while do`, `end call return break continue` | `Conditional`, `Repeat`, `Statement` |
| Stack effects | `(n: int -- int)` | `Delimiter`, `Identifier`, `Type` |
| Types and conversions | `int bool ptr u8`, `~ptr` | `Type` |
| Built-in words | `dup swap mod and shl` | `Function` |
| Hardware words | `outb cli lidt rdmsr` | `Special` |
| Operators | `+ - == <= @8 !64` | `Operator` |
| Literals | `42 -7 0xff 0b101`, `'A'`, `"text\n"`, `true` | `Number`, `Character`, `String`, `Boolean` |
| Addresses and arrays | `&handler`, `idt[]` | `Special`, `Identifier` |
| Libraries | `grab asm<io>` | `Include`, `String` |

Your colour scheme decides the actual colours.
