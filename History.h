// This is freeand unencumbered software released into the public domain.
// 
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non - commercial, and by any
// means.
// 
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain.We make this dedication for the benefit
// of the public at largeand to the detriment of our heirsand
// successors.We intend this dedication to be an overt act of
// relinquishment in perpetuity of all presentand future rights to this
// software under copyright law.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
// 
// For more information, please refer to < http://unlicense.org/>

#pragma once
#include <any>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <cassert>

template<typename... Args>
using DelegateType = std::function<bool(Args...)>;

struct History;

// History control object with operations stack.
struct HistoryContext
{
    HistoryContext(HistoryContext* parent = nullptr);

    // Ctrl+Y
    bool Redo();

    // Ctrl+Z
    bool Undo();

    // Checks whether currently in Undo() or Redo()
    bool IsUndoing() const;
    bool IsRedoing() const;
    bool IsUndoingOrRedoing() const;

    // Get current history object.
    History* Present() const;

    // @returns History that will become Present after Redo() or nullptr if can't Redo()
    History* PeekFuture() const;

    // Get parent context.
    HistoryContext* ParentContext() const;

    // Get read-only data.
    const auto& GetStackData() const { return m_HistoryStack; }

    // Dumps the current stack to string.
    std::string Dump(int indentCount = 0) const;

    // Create a new History object on the Stack.
    // @param name: Label for debug purposes
    // @param do_func: Delegate for future Redo operations. Not called immediately.
    // @param undo_func: Delegate for Undo operations.
    // @params args: Do / Undo function arguments to store and reuse.
    template<typename... Args>
    void Push(const std::string& name, DelegateType<Args...>&& do_func, DelegateType<Args...>&& undo_func, const std::decay_t<Args>&... args)
    {
        if (History::s_Lock)
            return;

        // May not push during undo/redo
        if (IsUndoingOrRedoing())
            return;

        PrePush();
        m_HistoryStack.push_back(new HistoryWithParams<Args...>(this, name, std::forward<DelegateType<Args...>>(do_func), std::forward<DelegateType<Args...>>(undo_func), args...));
    }

    // Use to remove the most recently created History object.
    void AbortPush();

    // Bind delegate to fire when the stack changes.
    void BindOnStackChanged(const std::function<void(int)>& func);

    // Unbind delegate on stack changes
    void UnbindOnStackChanged();

    // Wipe the stack.
    void Clear();

private:
    // Prepare the stack for a new object, deleting all operations above the Present.
    void PrePush();

    // The Undo stack.
    std::vector<History*> m_HistoryStack = std::vector<History*>(1);

    // Index to Present on the Stack.
    int m_PresentHistoryIdx = 0;

    // If true, is currently in Undo or Redo. 
    bool m_IsUndoing = false;
    bool m_IsRedoing = false;

    // Context this object resides in.
    HistoryContext* m_ParentContext = nullptr;

    // Event delegates
    std::function<void(int)> m_OnStackChanged;

    // Guard for preventing simultaneous Undo/Redo ops.
    std::mutex m_Mutex;

    template<typename... Args>
    friend struct HistoryWithParams;
    friend struct History;
    friend struct HistoryPushController;
    friend struct HistoryPopController;
};

// History base class. Exists on the History (Undo) Stack.
struct History
{
    // Blocks ALL history operations until released.
    static HistoryContext* GetContext();
    static void SetContext(HistoryContext* newContext);

    static void Disable();
    static void Enable();

    // Get topmost context.
    static HistoryContext* GetRootContext();

    History(HistoryContext* parentContext, const std::string& name)
        : m_Label(name)
        , m_SubContext(parentContext)
        , m_ID(NewID())
    {}
    virtual ~History() = default;

    // Save any kind of variable into this object
    // @param key: See HISTORY_KEY macro
    // @param value: Value to save
    template<typename T>
    bool Save(const std::string& key, const T& value)
    {
        if (History::s_Lock)
            return false;

        // May not save in undo / redo.
        if (m_SubContext.IsUndoingOrRedoing())
            return false;

        m_Data[key] = value;
        return true;
    }

    // Load a variable
    // @param key: See HISTORY_KEY macro
    // @param output: Variable is loaded here
    // @returns true if the variable was loaded successfully
    template<typename T>
    bool Load(const std::string& key, T& output)
    {
        if (History::s_Lock)
            return false;

        // May load only during undo/redo
        if (!m_SubContext.IsUndoingOrRedoing())
            return false;

        std::string id = key;

        size_t it = id.find("_Undo");
        if(it != std::string::npos)
            id.erase(it);

        if (m_Data.count(id) == 0)
            return false;

        output = std::any_cast<T>(m_Data[id]);
        return true;
    }

    const auto& GetLabel() const { return m_Label; }
    const auto& GetId() const { return m_ID; }
    const auto& GetSubcontext() const { return m_SubContext; }

protected:
    History() = default;

    static unsigned int NewID();

    // Global context used by all functionalities. Set this before using History.
    static HistoryContext* s_Context;

    // Global lock
    static bool s_Lock;

    // Undo / Redo interface
    virtual bool Redo() = 0;
    virtual bool Undo() = 0;

    // Everything stored via Save. All types of data go here.
    std::map<std::string, std::any> m_Data;

    // Readable name
    std::string m_Label;

    // Lookup ID
    unsigned int m_ID;

    // Holds History subobjects.
    HistoryContext m_SubContext;

    friend struct HistoryContext;
    friend struct HistoryPushController;
    friend struct HistoryPopController;
};

// Exact History implementation.
template<typename... Args>
struct HistoryWithParams : History
{
    using TupleType = std::tuple<std::decay_t<Args>...>;
    constexpr static size_t TupleSize = std::tuple_size_v<TupleType>;

    HistoryWithParams(HistoryContext* parentContext, const std::string& name, DelegateType<Args...>&& d, DelegateType<Args...>&& ud, Args... args)
        : History(parentContext, name)
        , m_DoFunc(d)
        , m_UndoFunc(ud)
        , m_Params(std::make_tuple(std::move(args)...))
    {
    }

    TupleType m_Params;
    DelegateType<Args...> m_DoFunc;
    DelegateType<Args...> m_UndoFunc;

protected:
    // Redo implementation. Calls m_DoFunc with all stored parameters.
    bool Redo() override
    {
        return Call(m_DoFunc, std::make_index_sequence<TupleSize>());
    }

    // Undo implementation. Calls m_UndoFunc with all stored parameters.
    bool Undo() override
    {
        return Call(m_UndoFunc, std::make_index_sequence<TupleSize>());
    }

    template<std::size_t... I>
    bool Call(const DelegateType<Args...>& func, const std::index_sequence<I...>& idxSeq)
    {
        return func(std::get<I>(m_Params)...);
    }
};

// Manages current history stack.
struct HistoryPushController
{
    HistoryPushController();
    ~HistoryPushController();

    bool active = true;
};

struct HistoryPopController
{
    HistoryPopController();
    ~HistoryPopController();
};

// Member functions
template<typename C, typename... Ts, std::size_t... I>
std::function<bool(Ts...)> hBindImpl(C* obj, bool(C::* func)(Ts...), std::index_sequence<I...>)
{
    return std::bind(func, obj, std::_Ph<I+1>()...);
}

template<typename C, typename... Ts, typename Indices = std::make_index_sequence<sizeof...(Ts)>>
std::function<bool(Ts...)> hBind(C* obj, bool(C::* func)(Ts...))
{
    return hBindImpl(obj, func, Indices());
}

// Free functions
template<typename... Ts, std::size_t... I>
std::function<bool(Ts...)> hBindImpl(bool(*func)(Ts...), std::index_sequence<I...>)
{
    return std::bind(func, std::_Ph<I + 1>()...);
}

template<typename... Ts, typename Indices = std::make_index_sequence<sizeof...(Ts)>>
std::function<bool(Ts...)> hBind(bool(*func)(Ts...))
{
    return hBindImpl(func, Indices());
}

// Push a new History object onto the stack.
// @param func: MEMBER Function name within which this is called.
// @param ...: func's parameters to store as copies for later use.
#define HISTORY_PUSH(func, ...) \
    assert(History::GetContext() && "You have to set history context first!"); \
    History::GetContext()->Push(#func, hBind(this, &std::decay<decltype(*this)>::type::##func##), hBind(this, &std::decay<decltype(*this)>::type::##func##_Undo), __VA_ARGS__); \
	HistoryPushController _use_HISTORY_PUSH_for_DoFunc_or_HISTORY_POP_for_UndoFunc;

#define HISTORY_PUSH_FREE(func, ...) \
    assert(History::GetContext() && "You have to set history context first!"); \
    History::GetContext()->Push(#func, hBind(func), hBind(func##_Undo), __VA_ARGS__); \
	HistoryPushController _use_HISTORY_PUSH_for_DoFunc_or_HISTORY_POP_for_UndoFunc;

#define HISTORY_ABORT_PUSH() \
    _use_HISTORY_PUSH_for_DoFunc_or_HISTORY_POP_for_UndoFunc.~HistoryPushController(); \
    History::GetContext()->AbortPush();

#define HISTORY_POP() \
    HistoryPopController _use_HISTORY_PUSH_for_DoFunc_or_HISTORY_POP_for_UndoFunc;

// Creates variable key for History storage.
#define HISTORY_KEY(var) std::string(#var)+"<-"+__FUNCTION__

// Save / Load macros
// Limitation: Does not work with shadowing. One name = one variable.
#define HISTORY_SAVE_UNSAFE(var) History::GetContext()->ParentContext()->Present()->Save(HISTORY_KEY(var), var)
#define HISTORY_SAVE2_UNSAFE(v1, v2) (HISTORY_SAVE_UNSAFE(v1) && HISTORY_SAVE_UNSAFE(v2))
#define HISTORY_SAVE3_UNSAFE(v1, v2, v3) (HISTORY_SAVE2_UNSAFE(v1, v2) && HISTORY_SAVE_UNSAFE(v3))
#define HISTORY_SAVE4_UNSAFE(v1, v2, v3, v4) (HISTORY_SAVE3_UNSAFE(v1, v2, v3) && HISTORY_SAVE_UNSAFE(v4))

#define HISTORY_LOAD_UNSAFE(var, ...) History::GetContext()->ParentContext()->Present()->Load(HISTORY_KEY(var), var, __VA_ARGS__)
#define HISTORY_LOAD2_UNSAFE(v1, v2, ...) (HISTORY_LOAD_UNSAFE(v1, __VA_ARGS__) && HISTORY_LOAD_UNSAFE(v2, __VA_ARGS__))
#define HISTORY_LOAD3_UNSAFE(v1, v2, v3, ...) (HISTORY_LOAD2_UNSAFE(v1, v2, __VA_ARGS__) && HISTORY_LOAD_UNSAFE(v3, __VA_ARGS__))
#define HISTORY_LOAD4_UNSAFE(v1, v2, v3, v4, ...) (HISTORY_LOAD3_UNSAFE(v1, v2, v3, __VA_ARGS__) && HISTORY_LOAD_UNSAFE(v4, __VA_ARGS__))

#define HISTORY_SAVE(var) _use_HISTORY_PUSH_for_DoFunc_or_HISTORY_POP_for_UndoFunc, HISTORY_SAVE_UNSAFE(var)
#define HISTORY_SAVE2(v1, v2) HISTORY_SAVE(v1) && HISTORY_SAVE_UNSAFE(v2)
#define HISTORY_SAVE3(v1, v2, v3) HISTORY_SAVE2(v1, v2) && HISTORY_SAVE_UNSAFE(v3)
#define HISTORY_SAVE4(v1, v2, v3, v4) HISTORY_SAVE3(v1, v2, v3) && HISTORY_SAVE_UNSAFE(v4);

#define HISTORY_LOAD(var, ...) (_use_HISTORY_PUSH_for_DoFunc_or_HISTORY_POP_for_UndoFunc, History::GetContext()->ParentContext()->Present()->Load(HISTORY_KEY(var), var, __VA_ARGS__))
#define HISTORY_LOAD2(v1, v2, ...) (HISTORY_LOAD(v1, __VA_ARGS__) && HISTORY_LOAD(v2, __VA_ARGS__))
#define HISTORY_LOAD3(v1, v2, v3, ...) (HISTORY_LOAD2(v1, v2, __VA_ARGS__) && HISTORY_LOAD(v3, __VA_ARGS__))
#define HISTORY_LOAD4(v1, v2, v3, v4, ...) (HISTORY_LOAD3(v1, v2, v3, __VA_ARGS__) && HISTORY_LOAD(v4, __VA_ARGS__))

#undef DelegateType
