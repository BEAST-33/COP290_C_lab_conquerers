#include "avl.h"
#include <stdlib.h>

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

int avl_get_height(AVLTree root) {
    return root ? root->height : 0;
}

static AVLTree new_node(int key) {
    AVLTree node = malloc(sizeof(AVLNode));
    if (!node) return NULL;
    node->key = key;
    node->left = node->right = NULL;
    node->height = 1; // New node is a leaf
    return node;
}

static AVLTree right_rotate(AVLTree y) {
    AVLTree x = y->left;
    AVLTree T2 = x->right;
    
    x->right = y;
    y->left = T2;
    
    y->height = max_int(avl_get_height(y->left), avl_get_height(y->right)) + 1;
    x->height = max_int(avl_get_height(x->left), avl_get_height(x->right)) + 1;
    
    return x;
}

static AVLTree left_rotate(AVLTree x) {
    AVLTree y = x->right;
    AVLTree T2 = y->left;
    
    y->left = x;
    x->right = T2;
    
    x->height = max_int(avl_get_height(x->left), avl_get_height(x->right)) + 1;
    y->height = max_int(avl_get_height(y->left), avl_get_height(y->right)) + 1;
    
    return y;
}

static int get_balance(AVLTree root) {
    return root ? avl_get_height(root->left) - avl_get_height(root->right) : 0;
}

AVLTree avl_insert(AVLTree root, int key) {
    if (root == NULL)
        return new_node(key);
    
    if (key < root->key)
        root->left = avl_insert(root->left, key);
    else if (key > root->key)
        root->right = avl_insert(root->right, key);
    else
        return root; // Duplicate keys not allowed

    root->height = 1 + max_int(avl_get_height(root->left), avl_get_height(root->right));
    int balance = get_balance(root);

    // Left Left Case
    if (balance > 1 && key < root->left->key)
        return right_rotate(root);
    // Right Right Case
    if (balance < -1 && key > root->right->key)
        return left_rotate(root);
    // Left Right Case
    if (balance > 1 && key > root->left->key) {
        root->left = left_rotate(root->left);
        return right_rotate(root);
    }
    // Right Left Case
    if (balance < -1 && key < root->right->key) {
        root->right = right_rotate(root->right);
        return left_rotate(root);
    }
    return root;
}

static AVLNode* min_value_node(AVLNode* node) {
    AVLNode* current = node;
    while (current->left != NULL)
        current = current->left;
    return current;
}

AVLTree avl_delete(AVLTree root, int key) {
    if (root == NULL)
        return root;
    
    if (key < root->key)
        root->left = avl_delete(root->left, key);
    else if (key > root->key)
        root->right = avl_delete(root->right, key);
    else {
        if ((root->left == NULL) || (root->right == NULL)) {
            AVLTree temp = root->left ? root->left : root->right;
            if (temp == NULL) {
                temp = root;
                root = NULL;
            } else {
                *root = *temp;
            }
            free(temp);
        } else {
            AVLNode* temp = min_value_node(root->right);
            root->key = temp->key;
            root->right = avl_delete(root->right, temp->key);
        }
    }
    if (root == NULL)
        return root;
    
    root->height = 1 + max_int(avl_get_height(root->left), avl_get_height(root->right));
    int balance = get_balance(root);
    
    // Balance the tree
    if (balance > 1 && get_balance(root->left) >= 0)
        return right_rotate(root);
    if (balance > 1 && get_balance(root->left) < 0) {
        root->left = left_rotate(root->left);
        return right_rotate(root);
    }
    if (balance < -1 && get_balance(root->right) <= 0)
        return left_rotate(root);
    if (balance < -1 && get_balance(root->right) > 0) {
        root->right = right_rotate(root->right);
        return left_rotate(root);
    }
    return root;
}

AVLNode* avl_search(AVLTree root, int key) {
    if (root == NULL || root->key == key)
        return root;
    return (key < root->key) ? avl_search(root->left, key) : avl_search(root->right, key);
}

void avl_free(AVLTree root) {
    if (root) {
        avl_free(root->left);
        avl_free(root->right);
        free(root);
    }
}
