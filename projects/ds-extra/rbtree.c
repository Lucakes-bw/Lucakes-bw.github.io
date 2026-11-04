// 红黑树：插入、删除，检查五条性质；和 AVL 树比较高度、旋转次数和时间
#include <stdbool.h>
#include <string.h>

#include "common.h"

/* ---------- 红黑树（《算法导论》的写法，用一个哨兵节点 NIL 代替空指针） ---------- */

typedef enum { RED, BLACK } Color;

typedef struct RBNode {
    int key;
    Color color;
    struct RBNode *left, *right, *parent;
} RBNode;

static RBNode nilNode = {0, BLACK, NULL, NULL, NULL};
#define NIL (&nilNode)

typedef struct {
    RBNode* root;
    long long rotations;
} RBTree;

static void rbInit(RBTree* t)
{
    t->root = NIL;
    t->rotations = 0;
}

// 左旋：x 的右孩子 y 成为这棵子树的新根，x 成为 y 的左孩子
static void leftRotate(RBTree* t, RBNode* x)
{
    RBNode* y = x->right;
    x->right = y->left;
    if (y->left != NIL) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL) t->root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
    ++t->rotations;
}

static void rightRotate(RBTree* t, RBNode* y)
{
    RBNode* x = y->left;
    y->left = x->right;
    if (x->right != NIL) x->right->parent = y;
    x->parent = y->parent;
    if (y->parent == NIL) t->root = x;
    else if (y == y->parent->right) y->parent->right = x;
    else y->parent->left = x;
    x->right = y;
    y->parent = x;
    ++t->rotations;
}

// 插入后修复：新节点 z 是红色的，可能和红色的父亲相邻
static void insertFixup(RBTree* t, RBNode* z)
{
    while (z->parent->color == RED) {
        RBNode* g = z->parent->parent;                  // 父亲是红色，一定不是根，所以祖父存在
        if (z->parent == g->left) {
            RBNode* uncle = g->right;
            if (uncle->color == RED) {                  // 情况 1：叔叔是红色，变色，问题上移到祖父
                z->parent->color = BLACK;
                uncle->color = BLACK;
                g->color = RED;
                z = g;
            } else {
                if (z == z->parent->right) {            // 情况 2：z 是右孩子，左旋父亲，变成情况 3
                    z = z->parent;
                    leftRotate(t, z);
                }
                z->parent->color = BLACK;               // 情况 3：z 是左孩子，右旋祖父，变色
                g->color = RED;
                rightRotate(t, g);
            }
        } else {                                        // 对称：父亲是右孩子
            RBNode* uncle = g->left;
            if (uncle->color == RED) {
                z->parent->color = BLACK;
                uncle->color = BLACK;
                g->color = RED;
                z = g;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rightRotate(t, z);
                }
                z->parent->color = BLACK;
                g->color = RED;
                leftRotate(t, g);
            }
        }
    }
    t->root->color = BLACK;                             // 根总是黑色
}

static bool rbInsert(RBTree* t, int key)
{
    RBNode *parent = NIL, *cur = t->root;
    while (cur != NIL) {
        parent = cur;
        if (key < cur->key) cur = cur->left;
        else if (key > cur->key) cur = cur->right;
        else return false;                              // 已经存在
    }
    RBNode* z = malloc(sizeof(RBNode));
    if (z == NULL) exit(1);
    *z = (RBNode){key, RED, NIL, NIL, parent};          // 新节点是红色的
    if (parent == NIL) t->root = z;
    else if (key < parent->key) parent->left = z;
    else parent->right = z;
    insertFixup(t, z);
    return true;
}

static RBNode* rbFind(const RBTree* t, int key)
{
    RBNode* cur = t->root;
    while (cur != NIL && cur->key != key) cur = key < cur->key ? cur->left : cur->right;
    return cur;
}

// 用 v 替换 u 在树中的位置
static void transplant(RBTree* t, RBNode* u, RBNode* v)
{
    if (u->parent == NIL) t->root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else u->parent->right = v;
    v->parent = u->parent;                              // v 可能是 NIL，哨兵的 parent 也要设置
}

// 删除后修复：x 多背了一层「黑色」
static void deleteFixup(RBTree* t, RBNode* x)
{
    while (x != t->root && x->color == BLACK) {
        if (x == x->parent->left) {
            RBNode* w = x->parent->right;               // 兄弟
            if (w->color == RED) {                      // 情况 1：兄弟是红色，转成兄弟是黑色
                w->color = BLACK;
                x->parent->color = RED;
                leftRotate(t, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {   // 情况 2：兄弟的两个孩子都是黑色
                w->color = RED;
                x = x->parent;                          // 多出的黑色上移
            } else {
                if (w->right->color == BLACK) {         // 情况 3：兄弟的右孩子是黑色，转成情况 4
                    w->left->color = BLACK;
                    w->color = RED;
                    rightRotate(t, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;            // 情况 4：兄弟的右孩子是红色，旋转后结束
                x->parent->color = BLACK;
                w->right->color = BLACK;
                leftRotate(t, x->parent);
                x = t->root;
            }
        } else {                                        // 对称
            RBNode* w = x->parent->left;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                rightRotate(t, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == BLACK && w->left->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->left->color == BLACK) {
                    w->right->color = BLACK;
                    w->color = RED;
                    leftRotate(t, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                rightRotate(t, x->parent);
                x = t->root;
            }
        }
    }
    x->color = BLACK;
}

static bool rbDelete(RBTree* t, int key)
{
    RBNode* z = rbFind(t, key);
    if (z == NIL) return false;
    RBNode *y = z, *x;
    Color yOriginal = y->color;
    if (z->left == NIL) {
        x = z->right;
        transplant(t, z, z->right);
    } else if (z->right == NIL) {
        x = z->left;
        transplant(t, z, z->left);
    } else {                                            // 两个孩子：用后继 y 代替 z
        y = z->right;
        while (y->left != NIL) y = y->left;
        yOriginal = y->color;
        x = y->right;
        if (y->parent == z) {
            x->parent = y;
        } else {
            transplant(t, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        transplant(t, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }
    free(z);
    if (yOriginal == BLACK) deleteFixup(t, x);          // 删掉（或移走）的是黑色节点，黑高被破坏了
    return true;
}

// 检查五条性质，返回黑高；有问题时返回 -1
static int checkRB(const RBNode* n, int lo, int hi, int* count)
{
    if (n == NIL) return 1;
    if (n->key <= lo || n->key >= hi) return -1;                              // 二叉搜索树
    if (n->color == RED && (n->left->color == RED || n->right->color == RED)) return -1;   // 红色节点的孩子是黑色
    ++*count;
    int l = checkRB(n->left, lo, n->key, count), r = checkRB(n->right, n->key, hi, count);
    if (l < 0 || r < 0 || l != r) return -1;                                  // 每条路径的黑色节点数相同
    return l + (n->color == BLACK);
}

static int rbHeight(const RBNode* n)
{
    if (n == NIL) return 0;
    int l = rbHeight(n->left), r = rbHeight(n->right);
    return 1 + (l > r ? l : r);
}

static void rbFreeAll(RBNode* n)
{
    if (n == NIL) return;
    rbFreeAll(n->left);
    rbFreeAll(n->right);
    free(n);
}

static void printTree(const RBNode* n, int depth)   // 横着打印：右子树在上
{
    if (n == NIL) return;
    printTree(n->right, depth + 1);
    printf("%*s%d%s\n", depth * 6, "", n->key, n->color == RED ? "(红)" : "(黑)");
    printTree(n->left, depth + 1);
}

/* ---------- AVL 树（和《AVL 树》一文相同，只加了旋转计数和删除） ---------- */

typedef struct AVLNode {
    int key, height;
    struct AVLNode *left, *right;
} AVLNode;

static long long avlRotations;

static int h(const AVLNode* n) { return n ? n->height : 0; }
static void update(AVLNode* n) { n->height = 1 + (h(n->left) > h(n->right) ? h(n->left) : h(n->right)); }
static AVLNode* rotR(AVLNode* y)
{
    AVLNode* x = y->left;
    y->left = x->right;
    x->right = y;
    update(y);
    update(x);
    ++avlRotations;
    return x;
}
static AVLNode* rotL(AVLNode* x)
{
    AVLNode* y = x->right;
    x->right = y->left;
    y->left = x;
    update(x);
    update(y);
    ++avlRotations;
    return y;
}
static AVLNode* rebalance(AVLNode* n)
{
    update(n);
    int bf = h(n->left) - h(n->right);
    if (bf > 1) {
        if (h(n->left->left) < h(n->left->right)) n->left = rotL(n->left);
        return rotR(n);
    }
    if (bf < -1) {
        if (h(n->right->right) < h(n->right->left)) n->right = rotR(n->right);
        return rotL(n);
    }
    return n;
}
static AVLNode* avlInsert(AVLNode* n, int key)
{
    if (n == NULL) {
        n = malloc(sizeof(AVLNode));
        if (n == NULL) exit(1);
        *n = (AVLNode){key, 1, NULL, NULL};
        return n;
    }
    if (key < n->key) n->left = avlInsert(n->left, key);
    else if (key > n->key) n->right = avlInsert(n->right, key);
    else return n;
    return rebalance(n);
}
static AVLNode* avlDelete(AVLNode* n, int key)
{
    if (n == NULL) return NULL;
    if (key < n->key) n->left = avlDelete(n->left, key);
    else if (key > n->key) n->right = avlDelete(n->right, key);
    else {
        if (n->left == NULL || n->right == NULL) {
            AVLNode* child = n->left ? n->left : n->right;
            free(n);
            return child;
        }
        AVLNode* s = n->right;
        while (s->left) s = s->left;
        n->key = s->key;
        n->right = avlDelete(n->right, s->key);
    }
    return rebalance(n);
}
static bool avlFind(const AVLNode* n, int key)
{
    while (n && n->key != key) n = key < n->key ? n->left : n->right;
    return n != NULL;
}
static void avlFreeAll(AVLNode* n)
{
    if (!n) return;
    avlFreeAll(n->left);
    avlFreeAll(n->right);
    free(n);
}

/* ---------- 测试 ---------- */

static void shuffle(int* a, int n)
{
    for (int i = n - 1; i > 0; --i) {
        int j = randInt(0, i), t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static void bench(const char* name, int* keys, int n)
{
    RBTree t;
    rbInit(&t);
    AVLNode* avl = NULL;
    avlRotations = 0;

    double s = nowSeconds();
    for (int i = 0; i < n; ++i) rbInsert(&t, keys[i]);
    double rbIns = nowSeconds() - s;
    long long rbRotIns = t.rotations;
    s = nowSeconds();
    for (int i = 0; i < n; ++i) avl = avlInsert(avl, keys[i]);
    double avlIns = nowSeconds() - s;
    long long avlRotIns = avlRotations;
    int count = 0;
    CHECK(checkRB(t.root, -1, 1 << 30, &count) > 0 && count == n);

    s = nowSeconds();
    for (int i = 0; i < n; ++i) CHECK(rbFind(&t, keys[i]) != NIL);
    double rbFindT = nowSeconds() - s;
    s = nowSeconds();
    for (int i = 0; i < n; ++i) CHECK(avlFind(avl, keys[i]));
    double avlFindT = nowSeconds() - s;

    int rbH = rbHeight(t.root), avlH = h(avl);
    long long r0 = t.rotations, a0 = avlRotations;
    s = nowSeconds();
    for (int i = 0; i < n / 2; ++i) rbDelete(&t, keys[i]);
    double rbDel = nowSeconds() - s;
    s = nowSeconds();
    for (int i = 0; i < n / 2; ++i) avl = avlDelete(avl, keys[i]);
    double avlDel = nowSeconds() - s;
    count = 0;
    CHECK(checkRB(t.root, -1, 1 << 30, &count) > 0 && count == n - n / 2);

    printf("%s n=%d\n", name, n);
    printf("  红黑树：高度 %2d，插入旋转 %8lld 次 %.3fs，查找 %.3fs，删除一半旋转 %7lld 次 %.3fs\n", rbH, rbRotIns, rbIns,
           rbFindT, t.rotations - r0, rbDel);
    printf("  AVL 树：高度 %2d，插入旋转 %8lld 次 %.3fs，查找 %.3fs，删除一半旋转 %7lld 次 %.3fs\n", avlH, avlRotIns, avlIns,
           avlFindT, avlRotations - a0, avlDel);
    rbFreeAll(t.root);
    avlFreeAll(avl);
}

int main(void)
{
    printf("== 有序插入 1..10\n");
    RBTree t;
    rbInit(&t);
    for (int k = 1; k <= 10; ++k) rbInsert(&t, k);
    printTree(t.root, 0);
    int count = 0;
    // checkRB 把根和 NIL 都算上了；黑高的定义不算节点自己（根是黑色），所以减一
    printf("黑高 %d，高度 %d，旋转 %lld 次\n", checkRB(t.root, -1, 1 << 30, &count) - 1, rbHeight(t.root), t.rotations);
    rbFreeAll(t.root);

    // 随机插入、删除，每一步之后检查五条性质
    rbInit(&t);
    bool present[2000] = {false};
    int size = 0;
    for (int step = 0; step < 200000; ++step) {
        int k = randInt(1, 1999);
        if (randInt(0, 1)) {
            if (rbInsert(&t, k)) ++size;
            present[k] = true;
        } else {
            if (rbDelete(&t, k)) --size;
            present[k] = false;
        }
        if (step % 1000 == 0) {
            count = 0;
            CHECK(checkRB(t.root, 0, 2000, &count) > 0 && count == size);
            for (int x = 1; x < 2000; ++x) CHECK((rbFind(&t, x) != NIL) == present[x]);
        }
    }
    printf("随机插入、删除 20 万次，每 1000 次检查一遍五条性质和全部元素：通过\n");
    rbFreeAll(t.root);

    int n = 1000000;
    int* keys = malloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; ++i) keys[i] = i;
    bench("有序插入", keys, n);
    shuffle(keys, n);
    bench("随机插入", keys, n);
    free(keys);
    return 0;
}
