class Trie {
    struct Node {
        Node* child[26] = {};
        bool end = false;
    };
    Node* root = new Node();
    Node* walk(const string& s) {
        Node* p = root;
        for (char c : s) {
            p = p->child[c - 'a'];
            if (!p) return nullptr;
        }
        return p;
    }
public:
    Trie() {}

    void insert(string word) {
        Node* p = root;
        for (char c : word) {
            if (!p->child[c - 'a']) p->child[c - 'a'] = new Node();
            p = p->child[c - 'a'];
        }
        p->end = true;
    }

    bool search(string word) {
        Node* p = walk(word);
        return p && p->end;
    }

    bool startsWith(string prefix) {
        return walk(prefix) != nullptr;
    }
};
