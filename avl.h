#ifndef AVL_H
#define AVL_H

typedef struct AVLNode {
    int key;                   // key (for example, encoded as row*total_cols + col)
    struct AVLNode* left;
    struct AVLNode* right;
    int height;
} AVLNode;

typedef AVLNode* AVLTree;

// Inserts key into the AVL tree rooted at root.
AVLTree avl_insert(AVLTree root, int key);

// Deletes key from the AVL tree rooted at root.
AVLTree avl_delete(AVLTree root, int key);

// Searches for a node with key in the AVL tree.
AVLNode* avl_search(AVLTree root, int key);

// Frees all nodes in the AVL tree.
void avl_free(AVLTree root);

// Returns the height of the tree.
int avl_get_height(AVLTree root);

#endif // AVL_H
