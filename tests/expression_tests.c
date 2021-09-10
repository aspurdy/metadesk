//$ exe //

#include "md.h"
#include "md.c"

MD_Arena *arena = 0;

typedef struct Expression_QA Expression_QA;
struct Expression_QA
{
    char *q;
    char *a;
};

typedef struct OperatorDescription OperatorDescription;
struct OperatorDescription{
    MD_String8 s;
    MD_ExprOperator op;
};

// NOTE(mal): Based on https://en.wikipedia.org/wiki/Operators_in_C_and_C%2B%2B#Operator_precedence 2021-09-05
//            Precedences are flipped. The ones listed here are 20-x where x is the precedence on the wikipedia table
#define OPERATORS \
    X(PostFixIncrement,    "++",        Postfix,                18) \
    X(PostFixDecrement,    "--",        Postfix,                18) \
    X(Call,                "()",        Postfix,                18) \
    X(ArraySubscript,      "[]",        Binary,                 18) \
    X(Member,              ".",         Binary,                 18) \
    X(PointerMember,       "->",        Binary,                 18) \
    X(PreFixIncrement,     "++",        Prefix,                 17) \
    X(PreFixDecrement,     "--",        Prefix,                 17) \
    X(UnaryPlus,           "+",         Prefix,                 17) \
    X(UnaryMinus,          "-",         Prefix,                 17) \
    X(LogicalNot,          "!",         Prefix,                 17) \
    X(BitwiseNot,          "~",         Prefix,                 17) \
    X(Dereference,         "*",         Prefix,                 17) \
    X(AddressOf,           "&",         Prefix,                 17) \
    X(SizeOf,              "sizeof",    Prefix,                 17) /* NOTE(mal); Just for fun*/ \
    X(Multiplication,      "*",         Binary,                 15) \
    X(Division,            "/",         Binary,                 15) \
    X(Modulo,              "%",         Binary,                 15) \
    X(Addition,            "+",         Binary,                 14) \
    X(Subtraction,         "-",         Binary,                 14) \
    X(LeftShift,           "<<",        Binary,                 13) \
    X(RightShift,          ">>",        Binary,                 13) \
    X(LessThan,            "<",         Binary,                 11) \
    X(LessThanOrEqual,     "<=",        Binary,                 11) \
    X(GreaterThan,         ">",         Binary,                 11) \
    X(GreaterThanOrEqual,  ">=",        Binary,                 11) \
    X(Equal,               "==",        Binary,                 10) \
    X(NotEqual,            "!=",        Binary,                 10) \
    X(BitwiseAnd,          "&",         Binary,                  9) \
    X(BitwiseXor,          "^",         Binary,                  8) \
    X(BitwiseOr,           "|",         Binary,                  7) \
    X(LogicalAnd,          "&&",        Binary,                  6) \
    X(LogicalOr,           "||",        Binary,                  5) \
    X(Assign,              "=",         BinaryRightAssociative,  4) \
    X(AssignAddition,      "+=",        BinaryRightAssociative,  4) \
    X(AssignSubtraction,   "-=",        BinaryRightAssociative,  4) \
    X(AssignMultiplication,"*=",        BinaryRightAssociative,  4) \
    X(AssignDivision,      "/=",        BinaryRightAssociative,  4) \
    X(AssignModulo,        "%=",        BinaryRightAssociative,  4) \
    X(AssignLeftShift,     "<<=",       BinaryRightAssociative,  4) \
    X(AssignRightShift,    ">>=",       BinaryRightAssociative,  4) \
    X(AssignBitwiseAnd,    "&=",        BinaryRightAssociative,  4) \
    X(AssignBitwiseXor,    "^=",        BinaryRightAssociative,  4) \
    X(AssignBitwiseOr,     "|=",        BinaryRightAssociative,  4) \
    X(BracketSet,          "[]",        Prefix,                  3) \
    X(BraceSet,            "{}",        Prefix,                  3)
    // X(Cast                 "()",        Prefix,                 17)
    // X(Comma,               ",",         Binary,                  3)

#define X(name, token, kind, prec) Op_##name,
typedef enum{
    Op_Null,
    OPERATORS
    Op_COUNT
} Op;
#undef X

static MD_String8 node_raw_contents(MD_Node *node, MD_b32 exclude_outer){
    MD_String8 result = {0};

    MD_u64 beg = node->offset;
    if(exclude_outer && !MD_NodeIsNil(node->first_child)){
        beg = node->first_child->offset;
    }

    MD_u64 end = beg; {
        MD_Node *last_descendant = node;
        while(!MD_NodeIsNil(last_descendant->last_child)){
            last_descendant = last_descendant->last_child;
        }
        end = last_descendant->offset + last_descendant->raw_string.size;
    }

    MD_Node *root = node;
    while(!MD_NodeIsNil(root->parent)){
        root = root->parent;
    }

    result = MD_S8Substring(root->raw_string, beg, end);
    return result;
}

static void parenthesize_exclude_outer(MD_Arena *arena, OperatorDescription *descs, MD_String8List *l, 
                                       MD_ExprNode *node, MD_b32 exclude_outer_parens){
    if(node->is_op){
        if(!exclude_outer_parens){
            MD_S8ListPush(arena, l, MD_S8Lit("("));
        }

        MD_ExprOperator *op = &descs[node->op_id].op;
        if(op->kind == MD_ExprOperatorKind_Binary || op->kind == MD_ExprOperatorKind_BinaryRightAssociative){

            parenthesize_exclude_outer(arena, descs, l, node->left, 0);

            MD_b32 is_subscript = (node->op_id == Op_ArraySubscript);
            if(is_subscript){
                MD_S8ListPush(arena, l, MD_S8Lit("["));
                parenthesize_exclude_outer(arena, descs, l, node->right, 1);
                MD_S8ListPush(arena, l, MD_S8Lit("]"));
            }
            else{
                MD_S8ListPush(arena, l, MD_S8Lit(" "));
                MD_S8ListPush(arena, l, node->md_node->string);
                MD_S8ListPush(arena, l, MD_S8Lit(" "));
                parenthesize_exclude_outer(arena, descs, l, node->right, 0);
            }
        }
        else if(op->kind == MD_ExprOperatorKind_Prefix){
            MD_S8ListPush(arena, l, node->md_node->string);
            MD_u8 last_op_c = MD_S8Suffix(node->md_node->string, 1).str[0];

            if(MD_CharIsAlpha(last_op_c) || MD_CharIsDigit(last_op_c)){ // NOTE: Keyword prefix operator (e.g. sizeof)
                MD_S8ListPush(arena, l, MD_S8Lit(" "));
            }

            parenthesize_exclude_outer(arena, descs, l, node->left, 0);
        }
        else if(op->kind == MD_ExprOperatorKind_Postfix){
            parenthesize_exclude_outer(arena, descs, l, node->left, 0);

            MD_b32 is_call = (node->op_id == Op_Call);
            if(is_call){
                MD_S8ListPush(arena, l, MD_S8Lit("(...)"));
            }
            else{
                MD_S8ListPush(arena, l, node->md_node->string);
            }
        }
        else{
            MD_S8ListPush(arena, l, MD_S8Lit("--- Can't print expression ---"));
        }

        if(!exclude_outer_parens){
            MD_S8ListPush(arena, l, MD_S8Lit(")"));
        }
    }
    else{
        if(MD_NodeIsNil(node->md_node->first_child)){
            MD_S8ListPush(arena, l, node_raw_contents(node->md_node, 0));
        }
        else{
            if(node->md_node->flags & MD_NodeFlag_HasBracketLeft && 
               node->md_node->flags & MD_NodeFlag_HasBracketRight){
                MD_S8ListPush(arena, l, MD_S8Lit("[...]"));
            }
            else if(node->md_node->flags & MD_NodeFlag_HasBraceLeft && 
               node->md_node->flags & MD_NodeFlag_HasBraceRight){
                MD_S8ListPush(arena, l, MD_S8Lit("{...}"));
            }
            else{
                MD_S8ListPush(arena, l, MD_S8Lit("???"));
            }
        }
    }
}

static MD_String8 parenthesize(MD_Arena *arena, OperatorDescription *descs, MD_ExprNode *node){
    MD_String8 result = {0};
    MD_String8List l = {0};
    parenthesize_exclude_outer(arena, descs, &l, node, 1);
    result = MD_S8ListJoin(arena, l, 0);
    return result;
}

int main(void){
    OperatorDescription operator_array[Op_COUNT] = {0};
#define X(name, token, kind_, prec) \
    operator_array[Op_##name].s = MD_S8Lit(token); \
    operator_array[Op_##name].op = (MD_ExprOperator){ .op_id = Op_##name, .kind = MD_ExprOperatorKind_##kind_, .precedence = prec };
    OPERATORS
#undef X 

    arena = MD_ArenaAlloc(1ull << 40);

    /* NOTE: Operator table bake errors */ {
        MD_ExprOperatorList operator_list = {0};
        MD_ExprOperatorTable op_table = {0};

        MD_String8 plus = MD_S8Lit("+");
        MD_String8 minus = MD_S8Lit("-");
        MD_Node *plus_node = MD_MakeNode(arena, MD_NodeKind_Main, plus, plus, 0);
        MD_Node *minus_node = MD_MakeNode(arena, MD_NodeKind_Main, minus, minus, 0);
        MD_Node *plus_node_bis = MD_MakeNode(arena, MD_NodeKind_Main, plus, plus, 0);

        // NOTE: Wrong operator kind
        operator_list = (MD_ExprOperatorList){0};
        MD_ExprOperatorPush(arena, &operator_list, Op_Addition, MD_ExprOperatorKind_Null, 1, plus_node);
        op_table = MD_ExprBakeOperatorTableFromList(arena, &operator_list);
        MD_Assert(op_table.errors.max_message_kind = MD_MessageKind_Warning && op_table.errors.node_count == 1 && 
                  op_table.errors.first->node == plus_node);

        // NOTE: Repeat operator
        operator_list = (MD_ExprOperatorList){0};
        MD_ExprOperatorPush(arena, &operator_list, Op_Addition, MD_ExprOperatorKind_Binary, 1, plus_node);
        MD_ExprOperatorPush(arena, &operator_list, Op_Addition, MD_ExprOperatorKind_Binary, 1, plus_node_bis);
        op_table = MD_ExprBakeOperatorTableFromList(arena, &operator_list);
        MD_Assert(op_table.errors.max_message_kind = MD_MessageKind_Warning && op_table.errors.node_count == 1 && 
                  op_table.errors.first->node == plus_node_bis);

        operator_list = (MD_ExprOperatorList){0};
        // NOTE: Binary-postfix operator conflict
        MD_ExprOperatorPush(arena, &operator_list, Op_Addition, MD_ExprOperatorKind_Binary, 1, plus_node);
        MD_ExprOperatorPush(arena, &operator_list, Op_Addition, MD_ExprOperatorKind_Postfix, 1, plus_node_bis);
        op_table = MD_ExprBakeOperatorTableFromList(arena, &operator_list);
        MD_Assert(op_table.errors.max_message_kind = MD_MessageKind_Warning && op_table.errors.node_count == 1 && 
                  op_table.errors.first->node == plus_node_bis);

        operator_list = (MD_ExprOperatorList){0};
        // NOTE: Same precedence difference associativity conflict
        MD_ExprOperatorPush(arena, &operator_list, Op_Addition, MD_ExprOperatorKind_Binary, 1, plus_node);
        MD_ExprOperatorPush(arena, &operator_list, Op_Addition, MD_ExprOperatorKind_BinaryRightAssociative, 
                            1, minus_node);
        op_table = MD_ExprBakeOperatorTableFromList(arena, &operator_list);
        MD_Assert(op_table.errors.max_message_kind = MD_MessageKind_Warning && op_table.errors.node_count == 1 && 
                  op_table.errors.first->node == minus_node);

    }

    MD_ExprOperatorList operator_list = {0};

    for(Op op = Op_Null+1; op < Op_COUNT; ++op){
        OperatorDescription *desc = operator_array + op;
        MD_Node *node = MD_MakeNode(arena, MD_NodeKind_Main, desc->s, desc->s, 0);
        MD_ExprOperatorPush(arena, &operator_list, op, desc->op.kind, desc->op.precedence, node);
    }

    MD_ExprOperatorTable op_table = MD_ExprBakeOperatorTableFromList(arena, &operator_list);

    Expression_QA tests[] = {
        { .q = "a + b + c",     .a = "(a + b) + c"      },
        { .q = "a + (b + c)",   .a = "a + (b + c)"      },
        { .q = "-a * b + c",    .a = "((-a) * b) + c"   },
        { .q = "a * (b + c)",   .a = "a * (b + c)"      },
        { .q = "a = b + c",     .a = "a = (b + c)"      },
        { .q = "a(b,c)",        .a = "a(...)"           },
        { .q = "(a + b)()",     .a = "(a + b)(...)"     },
        { .q = "sizeof a + b",  .a = "(sizeof a) + b"   },
        { .q = "[1, 100] * n",  .a = "[...] * n"        },
        { .q = "a[b+c]",        .a = "a[b + c]"         },
        { .q = "a++ + b",       .a = "(a++) + b"        },
    };

    for(MD_u32 i_test = 0; i_test < MD_ArrayCount(tests); i_test+=1){
        Expression_QA test = tests[i_test];
        MD_String8 q = MD_S8CString(test.q);
        MD_String8 a = MD_S8CString(test.a);

        MD_ParseResult parse = MD_ParseWholeString(arena, MD_S8Lit("test"), q);
        MD_Assert(parse.errors.max_message_kind == MD_MessageKind_Null);
        MD_ExprParseResult expr_parse = MD_ExprParse(arena, &op_table, parse.node->first_child, MD_NilNode());

        if(expr_parse.errors.max_message_kind){
            printf("Example %d : \"%.*s\". Got error \"%.*s\"\n", i_test, MD_S8VArg(q),
                   MD_S8VArg(expr_parse.errors.first[0].string));
        }
        else{
            MD_String8 parser_answer = parenthesize(arena, operator_array, expr_parse.node);
            if(expr_parse.errors.max_message_kind || !MD_S8Match(parser_answer, a, 0)){
                printf("Example %d : Expected answer for %.*s is %.*s. Got %.*s\n", 
                       i_test, MD_S8VArg(q), MD_S8VArg(a), MD_S8VArg(parser_answer));
            }
        }
    }

    return 0;
}