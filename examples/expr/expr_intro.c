/* 
** Example: expressions intro
**
** TODO
**
*/

//~ includes and globals //////////////////////////////////////////////////////

#include "md.h"
#include "md.c"

static MD_Arena *arena = 0;

//~ expression setup and helpers //////////////////////////////////////////////

void
print_expression(FILE *out, MD_Expr *expr)
{
    // TODO(allen): finish
}


//~ main //////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
    // setup the global arena
    arena = MD_ArenaAlloc();
    
    // parse all files passed to the command line
    MD_Node *list = MD_MakeList(arena);
    for (int i = 1; i < argc; i += 1)
    {
        
        // parse the file
        MD_String8 file_name = MD_S8CString(argv[i]);
        MD_ParseResult parse_result = MD_ParseWholeFile(arena, file_name);
        
        // print metadesk errors
        for (MD_Message *message = parse_result.errors.first;
             message != 0;
             message = message->next)
        {
            MD_CodeLoc code_loc = MD_CodeLocFromNode(message->node);
            MD_PrintMessage(stdout, code_loc, message->kind, message->string);
        }
        
        // save to parse results list
        if (parse_result.errors.max_message_kind < MD_MessageKind_Error)
        {
            MD_PushNewReference(arena, list, parse_result.node);
        }
    }
    
    // setup the expression system
    MD_ExprOprTable table = {0};
    {
        MD_ExprOprList list = {0};
        
        MD_ExprOprPush(arena, &list, MD_ExprOprKind_Binary, 1, MD_S8Lit("+"), OpAdd, 0);
        MD_ExprOprPush(arena, &list, MD_ExprOprKind_Binary, 2, MD_S8Lit("*"), OpMul, 0);
        
        table = MD_ExprBakeOperatorTableFromList(arena, &list);
    }
    
    // print the verbose parse results
    for (MD_EachNode(root_it, list->first_child))
    {
        MD_Node *root = MD_ResolveNodeFromReference(root_it);
        for (MD_EachNode(node, root->first_child))
        {
            MD_ExprParseResult parse = MD_ExprParse(arena, &table, node->first_child, MD_NilNode());
            
            // print errors
            for (MD_Message *message = parse.errors.first;
                 message != 0;
                 message = message->next)
            {
                MD_CodeLoc code_loc = MD_CodeLocFromNode(message->node);
                MD_PrintMessage(stdout, code_loc, message->kind, message->string);
            }
            
            // print the expression
            fprintf(stdout, "%s = ");
            print_expression(stdout, parse.expr);
            fprintf(stdout, ";\n");
        }
    }
    
    return 0;
}
