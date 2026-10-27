class MinStack {
    vector<int> st, mins;   // mins.back() 是当前栈中的最小值
public:
    MinStack() {}

    void push(int val) {
        st.push_back(val);
        mins.push_back(mins.empty() ? val : min(val, mins.back()));
    }

    void pop() {
        st.pop_back();
        mins.pop_back();
    }

    int top() {
        return st.back();
    }

    int getMin() {
        return mins.back();
    }
};
