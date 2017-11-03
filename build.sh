#!/bin/bash

set -e
set -x

# Generate a Fortran AST from Fortran.asdl
python grammar/asdl_py.py

# Generate a parse tree from fortran.g4
antlr4="java org.antlr.v4.Tool"
(cd grammar; $antlr4 -Dlanguage=Python3 -no-listener -visitor fortran.g4 -o ../liblfort/parser)
