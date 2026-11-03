" Vim syntax file
" Language:    Stackexpr
" Filenames:   *.sxp
" Maintainer:  Stackexpr project

if exists("b:current_syntax")
  finish
endif

" Identifiers may contain '.', as in struct fields like Point.x.
syn iskeyword @,48-57,_,.
syn case match

" ---- Literals --------------------------------------------------------------
syn match   sxEscape      contained "\\\%([ntr0e\\'"]\|x\x\x\)"
syn region  sxString      start=+"+ skip=+\\\\\|\\"+ end=+"+ oneline contains=sxEscape,@Spell
syn match   sxChar        "'\%([^'\\]\|\\\%([ntr0e\\'"]\|x\x\x\)\)'" contains=sxEscape
syn match   sxNumber      "\<\d[0-9_]*\>"
syn match   sxNumber      "\<0[xX][0-9a-fA-F_]\+\>"
syn match   sxNumber      "\<0[bB][01_]\+\>"
syn keyword sxBoolean     true false

" ---- Keywords --------------------------------------------------------------
syn keyword sxInclude     grab nextgroup=sxLibrary skipwhite
syn match   sxLibrary     contained "asm<\k\+>"
syn keyword sxConditional if else
syn keyword sxRepeat      while do
syn keyword sxStatement   end call return break continue execute invoke let in set
syn keyword sxOperatorWord sizeof lengthof

syn keyword sxDefine      stackexpr interrupt nextgroup=sxDefName skipwhite
syn keyword sxStorage     inline extern
syn keyword sxStorage     const var data reserve align nextgroup=sxDefName skipwhite
syn keyword sxStructure   struct nextgroup=sxDefName skipwhite
syn match   sxDefName     contained "\k\+"

" ---- Built-in words --------------------------------------------------------
syn keyword sxBuiltin     dup drop swap over rot nip tuck
syn keyword sxBuiltin     mod udiv umod ult ugt ule uge and or xor not shl shr sar
syn keyword sxHardware    inb inw ind outb outw outd cli sti hlt lgdt lidt invlpg
syn keyword sxHardware    rdmsr wrmsr rdcr0 rdcr2 rdcr3 rdcr4 wrcr0 wrcr3 wrcr4

" Operator symbols, from longest to shortest.
syn match   sxOperator    "[@!]\%(8\|16\|32\|64\)\>"
syn match   sxOperator    "++\|==\|!=\|<=\|>=\|[-+*/<>]"
" A negative literal: the minus touches the digits (after the operators, so it wins).
syn match   sxNumber      "-\d[0-9_]*\>"

" ---- Types -----------------------------------------------------------------
syn keyword sxType        int bool ptr u8 u16 u32 u64
syn match   sxConversion  "\~\%(int\|bool\|ptr\)\>"
syn match   sxAddress     "&\k\+"
syn match   sxIndex       "\k\+\[\]"

" A stack effect: ( name: type ... -- type ... )
syn region  sxStackEffect matchgroup=sxStackEffectDelim start="(" end=")" contains=sxEffectName,sxEffectSep,sxType,sxTypeVar,sxComment
syn match   sxEffectName  contained "\k\+:"
syn match   sxEffectSep   contained "--"
syn match   sxTypeVar     contained "\<\l\>"

" ---- Comments --------------------------------------------------------------
" Defined last so they win over the '/' operator.
syn keyword sxTodo        contained TODO FIXME XXX NOTE
syn match   sxComment     "//.*$" contains=sxTodo,@Spell

" ---- Highlighting ----------------------------------------------------------
hi def link sxTodo            Todo
hi def link sxComment         Comment
hi def link sxEscape          SpecialChar
hi def link sxString          String
hi def link sxChar            Character
hi def link sxNumber          Number
hi def link sxBoolean         Boolean
hi def link sxInclude         Include
hi def link sxLibrary         String
hi def link sxConditional     Conditional
hi def link sxRepeat          Repeat
hi def link sxStatement       Statement
hi def link sxOperatorWord    Operator
hi def link sxDefine          Define
hi def link sxStorage         StorageClass
hi def link sxStructure       Structure
hi def link sxDefName         Function
hi def link sxBuiltin         Function
hi def link sxHardware        Special
hi def link sxOperator        Operator
hi def link sxType            Type
hi def link sxConversion      Type
hi def link sxAddress         Special
hi def link sxIndex           Identifier
hi def link sxStackEffectDelim Delimiter
hi def link sxEffectName      Identifier
hi def link sxEffectSep       Delimiter
hi def link sxTypeVar         Type

let b:current_syntax = "stackexpr"
