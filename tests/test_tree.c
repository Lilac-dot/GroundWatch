/* Checks that src/tree.c (generated) gives the same answer as the Python tree
 * on the vectors tools/train_tree.py exported alongside it. */
#include <stdio.h>
#include "tree.h"
#include "tree_vectors.h"

int main(void)
{
    int n = sizeof tree_vectors / sizeof tree_vectors[0], bad = 0;
    for (int i = 0; i < n; i++) {
        const uint16_t *v = tree_vectors[i].f;
        tree_feat_t f = { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9] };
        tree_f = f;
        if (tree_predict() != tree_vectors[i].ev) bad++;
    }
    printf(bad ? "tree: %d of %d vectors differ from Python\n" : "tree: all %d vectors match Python\n", bad ? bad : n, n);
    return bad != 0;
}
