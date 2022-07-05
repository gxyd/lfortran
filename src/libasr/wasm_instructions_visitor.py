import sys
import os
import re

HEAD = r"""#ifndef LFORTRAN_%(MOD)s_H
#define LFORTRAN_%(MOD)s_H

// Generated by grammar/wasm_instructions_visitor.py

#include <libasr/alloc.h>
#include <libasr/location.h>
#include <libasr/colors.h>
#include <libasr/containers.h>
#include <libasr/exception.h>
#include <libasr/asr_scopes.h>
#include <libasr/codegen/wasm_utils.h>


namespace LFortran {

namespace %(MOD)s {

"""

FOOT = r"""
} // namespace %(MOD)s

} // namespace LFortran

#endif // LFORTRAN_%(MOD)s_H
"""

class WASMInstructionsVisitor():
    def __init__(self, stream, data):
        self.stream = stream
        self.data = data

    def visitWASMInstructions(self, mod, *args):
        self.emit("template <class Derived>", 0)
        self.emit("class BaseWASMVisitor {", 0)
        self.emit("private:", 0)
        self.emit(    "Derived& self() { return static_cast<Derived&>(*this); }", 1)
        self.emit("public:", 0)
        self.emit(    "Vec<uint8_t> &code;", 1)
        self.emit(    "uint32_t offset;\n", 1)

        self.emit(    "BaseWASMVisitor(Vec<uint8_t> &code, uint32_t offset): code(code), offset(offset) {}", 1)
        
        for inst in mod["instructions"]:
            self.emit("void visit_%s(%s) {throw LFortran::LFortranException(\"visit_%s() not implemented\");}\n" % (inst["func"], make_param_list(inst["params"]), inst["func"]), 1)

        self.emit(    "void decode_instructions() {", 1)
        self.emit(        "uint8_t cur_byte = wasm::read_b8(code, offset);", 2)
        self.emit(        "while (cur_byte != 0x0B) {", 2)
        self.emit(            "switch (cur_byte) {", 3)
        for inst in filter(lambda i: i["opcode"] not in ["0xFC", "0xFD"], mod["instructions"]):
            self.emit(            "case %s: {" % (inst["opcode"]), 4)
            for param in inst["params"]:
                self.emit(            "%s %s = %s(code, offset);" % (param["type"], param["name"], param["read_func"]), 5)
            self.emit(                "self().visit_%s(%s);" % (inst["func"], make_param_list(inst["params"], call=True)), 5)
            self.emit(                "break;", 5)
            self.emit(            "}", 4)
        
        self.emit(                "case 0xFC: {", 4)
        self.emit(                    "uint32_t num = wasm::read_u32(code, offset);", 5)
        self.emit(                    "switch(num) {", 5)
        for inst in filter(lambda i: i["opcode"] == "0xFC", mod["instructions"]):
            self.emit(                    "case %sU: {" % (inst["params"][0]["val"]), 6)
            for param in inst["params"][1:]: # first param is already read right at the start of case 0xFC
                self.emit(                    "%s %s = %s(code, offset);" % (param["type"], param["name"], param["read_func"]), 7)
            self.emit(                        "self().visit_%s(%s);" % (inst["func"], make_param_list(inst["params"], call=True)), 7)
            self.emit(                        "break;", 7)
            self.emit(                    "}", 6)
        self.emit(                        "default: {", 6)
        self.emit(                            "throw LFortran::LFortranException(\"Unknown num for opcode 0xFC\");", 7)
        self.emit(                        "}", 6)
        self.emit(                    "}", 5)
        self.emit(                    "break;", 5)
        self.emit(                "}", 4)
        
        self.emit(                "case 0xFD: {", 4)
        self.emit(                    "uint32_t num = wasm::read_u32(code, offset);", 5)
        self.emit(                    "switch(num) {", 5)
        for inst in filter(lambda i: i["opcode"] == "0xFD", mod["instructions"]):
            self.emit(                    "case %sU: {" % (inst["params"][0]["val"]), 6)
            for param in inst["params"][1:]:  # first param is already read right at the start of case 0xFD
                self.emit(                    "%s %s = %s(code, offset);" % (param["type"], param["name"], param["read_func"]), 7)
            self.emit(                        "self().visit_%s(%s);" % (inst["func"], make_param_list(inst["params"], call=True)), 7)
            self.emit(                        "break;", 7)
            self.emit(                    "}", 6)
        self.emit(                        "default: {", 6)
        self.emit(                            "throw LFortran::LFortranException(\"Unknown num for opcode 0xFD\");", 7)
        self.emit(                        "}", 6)
        self.emit(                    "}", 5)
        self.emit(                    "break;", 5)
        self.emit(                "}", 4)

        self.emit(                "default: {", 4)
        self.emit(                    "throw LFortran::LFortranException(\"Unknown opcode\");", 5)
        self.emit(                "}", 4)
        self.emit(            "}", 3)
        self.emit(            "cur_byte = wasm::read_b8(code, offset);", 3)
        self.emit(        "}", 2)
        self.emit(    "}", 1)
        self.emit("};", 0)


    def emit(self, line, level=0):
        indent = "    "*level
        self.stream.write(indent + line + "\n")

def make_param_list(params, call = False):
    params = list(filter(lambda param: param["val"] != "0x00" and param["name"] != "num", params))
    if call:
        return ", ".join(map(lambda param: param["name"], params))
    return ", ".join(map(lambda param: param["type"] + " /*" + param["name"] + "*/", params))

def read_file(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()

def process_raw_instructions(instructions_raw):
    instructions = []
    for line in instructions_raw.splitlines():
        if line.startswith("--"):
            continue
        if len(instructions) > 0 and line.startswith(" "):
            instructions[-1].append(line.strip())
        else:
            instructions.append(line.strip())
    return instructions

def get_func_name(func):
    splitted_name = re.split("[\._]", func)
    return "".join(map(lambda name_sub_part: name_sub_part.capitalize(), splitted_name))

param_read_function = {
    "uint8_t": "wasm::read_b8",
    "uint32_t": "wasm::read_u32",
    "int32_t": "wasm::read_i32",
    "int64_t": "wasm::read_i64",
    "float": "wasm::read_f32",
    "double": "wasm::read_f64"
}

param_type = {
    "u8": "uint8_t",
    "u32": "uint32_t",
    "i32": "int32_t",
    "i64": "int64_t",
    "f32": "float",
    "f64": "double",
}

def parse_param_info(param_info):
    type_info, name, val = param_info.split(":")
    type = param_type[type_info]
    read_func = param_read_function[type]
    return {"type": type, "read_func": read_func, "name": name, "val": val}

def parse_instructions(instructions):
    instructions_info = []
    for inst in instructions:
        binary_info, text_info = inst.split(" ⇒ ")
        binary_info = binary_info.strip().split(" ")
        opcode = binary_info[0]
        params_info = binary_info[1:]
        text_info = text_info.strip().split(" ")
        func = get_func_name(text_info[0]) # first parameter is the function name
        text_params = text_info[1:] # text_params are currently not needed and hence not used further
        params = list(map(lambda param_info: parse_param_info(param_info), params_info))
        instructions_info.append({"opcode": opcode, "func": func, "params": params})
    return instructions_info

def main(argv):
    if len(argv) == 3:
        def_file, out_file = argv[1:]
    elif len(argv) == 1:
        print("Assuming default values of wasm_instructions.txt and wasm_visitor.h")
        here = os.path.dirname(__file__)
        def_file = os.path.join(here, "wasm_instructions.txt")
        out_file = os.path.join(here, "wasm_visitor.h")
    else:
        print("invalid arguments")
        return 2
    instructions_raw = read_file(def_file)
    instructions = process_raw_instructions(instructions_raw)
    instructions_info = parse_instructions(instructions)
    subs = {"MOD": "WASM_INSTS_VISITOR"}
    fp = open(out_file, "w", encoding="utf-8")
    try:
        fp.write(HEAD % subs)
        wasm_instructions_visitor = WASMInstructionsVisitor(fp, None)
        wasm_instructions_visitor.visitWASMInstructions({"instructions": instructions_info})
        fp.write(FOOT % subs)
    finally:
        fp.close()

if __name__ == "__main__":
    sys.exit(main(sys.argv))
