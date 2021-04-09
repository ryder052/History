#include "History.h"

HistoryContext* History::s_Context = nullptr;
bool History::s_Lock = false;

HistoryContext* History::GetContext()
{
    return s_Context;
}

void History::SetContext(HistoryContext* newContext)
{
    s_Context = newContext;
}

void History::Disable()
{
    s_Lock = true;
}

void History::Enable()
{
    s_Lock = false;
}

HistoryContext* History::GetRootContext()
{
    auto* context = GetContext();
    while (context->ParentContext())
        context = context->ParentContext();

    return context;
}

unsigned int History::NewID()
{
    static unsigned int id = 0;
    return ++id;
}

void HistoryContext::PrePush()
{
    if (History::s_Lock)
        return;

    // Increment Present index
    ++m_PresentHistoryIdx;

    // Clear Redos.
    while (m_HistoryStack.size() != m_PresentHistoryIdx)
    {
        delete m_HistoryStack.back();
        m_HistoryStack.pop_back();
    }
}

HistoryContext::HistoryContext(HistoryContext* parent /*= nullptr*/)
    : m_ParentContext(parent)
    , m_OnStackChanged([](int) {})
{
}

bool HistoryContext::Redo()
{
    if (History::s_Lock)
        return false;

    std::scoped_lock<std::mutex> lock(m_Mutex);

	if (m_PresentHistoryIdx == m_HistoryStack.size() - 1)
		return false;

    m_IsRedoing = true;
	bool result = m_HistoryStack[++m_PresentHistoryIdx]->Redo();
    m_IsRedoing = false;

    m_OnStackChanged(m_PresentHistoryIdx);

	return result;
}

bool HistoryContext::Undo()
{
    if (History::s_Lock)
        return false;

    std::scoped_lock<std::mutex> lock(m_Mutex);

	if (!m_PresentHistoryIdx)
		return false;

    m_IsUndoing = true;
	bool result = m_HistoryStack[m_PresentHistoryIdx]->Undo();
	--m_PresentHistoryIdx;
    m_IsUndoing = false;

    m_OnStackChanged(m_PresentHistoryIdx);

	return result;
}

bool HistoryContext::IsUndoing() const
{
    auto* context = this;
    while (true)
    {
        if (context->m_IsUndoing)
            return true;

        if (context->ParentContext())
            context = context->ParentContext();
        else
            return false;
    }
}

bool HistoryContext::IsRedoing() const
{
    auto* context = this;
    while (true)
    {
        if (context->m_IsRedoing)
            return true;

        if (context->ParentContext())
            context = context->ParentContext();
        else
            return false;
    }
}

bool HistoryContext::IsUndoingOrRedoing() const
{
    return IsUndoing() || IsRedoing();
}

History* HistoryContext::Present() const
{
    if (History::s_Lock)
        return nullptr;

    return m_HistoryStack[m_PresentHistoryIdx];
}

History* HistoryContext::PeekFuture() const
{
    if (History::s_Lock)
        return nullptr;

    if (m_PresentHistoryIdx < (int(m_HistoryStack.size()) - 1))
        return m_HistoryStack[m_PresentHistoryIdx + 1];
    else
        return nullptr;
}

HistoryContext* HistoryContext::ParentContext() const
{
    if (History::s_Lock)
        return nullptr;

    return m_ParentContext;
}

std::string HistoryContext::Dump(int indentCount /*=0*/) const
{
    std::string result;
    std::string tabs;
    for (int i = 0; i < indentCount; ++i)
        tabs += '\t';

    for (int i = int(m_HistoryStack.size()) - 1; i > 0; --i)
    {
        std::string record = tabs + m_HistoryStack[i]->m_Label;
        if (m_PresentHistoryIdx == i)
            record += " <<<";

        record += '\n';
        result += record;
        result += m_HistoryStack[i]->m_SubContext.Dump(indentCount + 1);
    }

    return result;
}

void HistoryContext::AbortPush()
{
    if (History::s_Lock)
        return;

    if (IsUndoingOrRedoing())
        return;

    --m_PresentHistoryIdx;
    m_HistoryStack.pop_back();
}

void HistoryContext::BindOnStackChanged(const std::function<void(int)>& func)
{
    if (History::s_Lock)
        return;

    m_OnStackChanged = func;
}

void HistoryContext::UnbindOnStackChanged()
{
    m_OnStackChanged = [](int) {};
}

void HistoryContext::Clear()
{
    if (History::s_Lock)
        return;

    m_PresentHistoryIdx = 0;
    m_HistoryStack = std::vector<History*>(1);
    m_OnStackChanged(0);
}

HistoryPushController::HistoryPushController()
{
    if (History::s_Lock)
        return;

    // No effect in Undo
    if (History::GetContext()->IsUndoing())
        return;

    // Push
    History::SetContext(&History::GetContext()->Present()->m_SubContext);
}

HistoryPushController::~HistoryPushController()
{
    if (History::s_Lock)
        return;

    // Already destroyed by ABORT_PUSH
    if (!active)
        return;

    // No effect in Undo
    if (History::GetContext()->IsUndoing())
        return;

    // Pop
    History::SetContext(History::GetContext()->ParentContext());

    // If still in subcontext and in Redo, move Preset ptr as in Do() if able
    if (History::GetContext()->ParentContext()
        && History::GetContext()->IsRedoing()
        && (History::GetContext()->m_PresentHistoryIdx < (int(History::GetContext()->m_HistoryStack.size()) - 1)))
    {
        ++History::GetContext()->m_PresentHistoryIdx;
    }
    else if(!History::GetContext()->IsRedoing())
    {
        History::GetContext()->m_OnStackChanged(History::GetContext()->m_PresentHistoryIdx);
    }

    active = false;
}

HistoryPopController::HistoryPopController()
{
    if (History::s_Lock)
        return;

    // Push
    History::SetContext(&History::GetContext()->Present()->m_SubContext);
}

HistoryPopController::~HistoryPopController()
{
    if (History::s_Lock)
        return;

    // Pop
    History::SetContext(History::GetContext()->ParentContext());

    // If still in subcontext, move Present ptr as in Undo() if able
    if (History::GetContext()->ParentContext() && History::GetContext()->m_PresentHistoryIdx > 1)
        --History::GetContext()->m_PresentHistoryIdx;
}
