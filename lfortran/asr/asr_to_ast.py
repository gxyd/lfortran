from ..ast import ast
from . import asr
from ..semantic import kinds

class ASR2ASTVisitor(asr.ASTVisitor):

    def visit_sequence(self, seq):
        r = []
        if seq is not None:
            for node in seq:
                r.append(self.visit(node))
        return r

    def visit_TranslationUnit(self, node):
        decl = []
        items = []
        for s in node.global_scope.symbols:
            sym = node.global_scope.symbols[s]
            if isinstance(sym, asr.Variable):
                stype = self.visit(sym.type)
                decl.append(ast.Declaration(vars=[
                    ast.decl(sym=sym.name, sym_type=stype)]))
            else:
                items.append(self.visit(sym))
        for item in node.items:
            items.append(self.visit(item))
        return ast.TranslationUnit(items=decl+items)

        for s in node.symtab.symbols:
            sym = node.symtab.symbols[s]
            if sym.dummy:
                continue
            stype = self.visit(sym.type)
            decl.append(ast.Declaration(vars=[
                ast.decl(sym=sym.name, sym_type=stype)]))

    def visit_Module(self, node):
        decl = []
        contains = []
        for s in node.symtab.symbols:
            sym = node.symtab.symbols[s]
            if isinstance(sym, asr.Function):
                if sym.body:
                    contains.append(self.visit(sym))
                else:
                    decl.append(
                        ast.Interface2(procs=[self.visit(sym)])
                    )
            else:
                raise NotImplementedError()
        return ast.Module(name=node.name, decl=decl, contains=contains)

    def visit_Assignment(self, node):
        target = self.visit(node.target)
        value = self.visit(node.value)
        return ast.Assignment(target, value)

    def visit_BinOp(self, node):
        left = self.visit(node.left)
        right = self.visit(node.right)
        if isinstance(node.op, asr.Add):
            op = ast.Add()
        elif isinstance(node.op, asr.Mul):
            op = ast.Mul()
        else:
            raise NotImplementedError()
        return ast.BinOp(left, op, right)

    def visit_Variable(self, node):
        return ast.Name(id=node.name)

    def visit_Num(self, node):
        return ast.Num(n=node.n)

    def visit_Integer(self, node):
        if node.kind == kinds.int32:
            return "integer"
        else:
            return "integer(kind=%d)" % node.kind

    def visit_Function(self, node):
        body = self.visit_sequence(node.body)
        args = []
        decl = []
        for arg in node.args:
            args.append(ast.arg(arg=arg.name))
            stype = self.visit(arg.type)
            attrs = []
            if arg.intent:
                attrs = [
                    ast.Attribute(name="intent",
                    args=[ast.attribute_arg(arg=arg.intent)]),
                ]
            dims = []
            for lb, ub in arg.type.dims:
                if lb:
                    lb = self.visit(lb)
                if ub:
                    ub = self.visit(ub)
                dims.append(ast.dimension(lb, ub))
            decl.append(ast.Declaration(vars=[
                ast.decl(sym=arg.name, sym_type=stype,
                attrs=attrs, dims=dims)]))
        for s in node.symtab.symbols:
            sym = node.symtab.symbols[s]
            if sym.dummy:
                continue
            stype = self.visit(sym.type)
            decl.append(ast.Declaration(vars=[
                ast.decl(sym=sym.name, sym_type=stype)]))
        return_type = self.visit(node.return_var.type)
        return_var = self.visit(node.return_var)
        return ast.Function(
            name=node.name, args=args, return_type=return_type,
                return_var=return_var,
            decl=decl, body=body)


def asr_to_ast(a):
    v = ASR2ASTVisitor()
    return v.visit(a)
