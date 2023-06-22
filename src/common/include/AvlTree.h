#ifndef AVLTREE_INCLUDED
#define AVLTREE_INCLUDED
 
typedef struct Tree *Tree;
typedef struct Node *Node;
 
Tree  Tree_New        (int (*comp)(void *, void *), void (*print)(void *));
 
void  Tree_Insert     (Tree t, void *data);
void  Tree_DeleteNode (Tree t, Node  node);
Node  Tree_SearchNode (Tree t, void *data);
Node Tree_SearchNode2 (Tree t, void *data);
 
Node  Tree_FirstNode  (Tree t);
Node  Tree_LastNode   (Tree t);
 
Node  Tree_PrevNode   (Tree t, Node n);
Node  Tree_NextNode   (Tree t, Node n);
 
void  Tree_Print      (Tree t);
 
void *Node_GetData (Node n);
 
#endif