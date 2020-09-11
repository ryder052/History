#include "History.h"
#include "Showcase.h"

ManagerBase::ManagerBase()
{
    History::SetContext(&context);
}

/// 
/// /////////////////////////////////////////////////////////////////////////////////
///

bool TrivialManager::AddNewObject()
{
    // Trivial registration
    HISTORY_PUSH(AddNewObject);
    objects.push_back(0);
    return true;
}

bool TrivialManager::AddNewObject_Undo()
{
    // WARNING: Always call this first in all Undo functions!!!
    HISTORY_POP();
    objects.pop_back();
    return true;
}

void HistoryShowcase_Basics()
{
    TrivialManager mgr;
    mgr.AddNewObject();

    assert(mgr.objects.size() == 1);
    History::GetContext()->Undo();
    assert(mgr.objects.size() == 0);
    History::GetContext()->Redo();
    assert(mgr.objects.size() == 1);
}

/// 
/// /////////////////////////////////////////////////////////////////////////////////
///

bool MapManager::AddObject(const std::string& key, int value)
{
    if (objects.find(key) != objects.end())
        return false;

    // Store function parameters as copies.
    // Undo / Redo invoked with same parameters.
    HISTORY_PUSH(AddObject, key, value);
    objects[key] = value;
    return true;
}

bool MapManager::AddObject_Undo(const std::string& key, int /*unused*/)
{
    HISTORY_POP();

    objects.erase(key);
    return true;
}

void HistoryShowcase_InlineParams()
{
    MapManager mgr;
    mgr.AddObject("foo", 11);

    assert(mgr.objects.size() == 1 && mgr.objects["foo"] == 11);
    History::GetContext()->Undo();
    assert(mgr.objects.size() == 0);
    History::GetContext()->Redo();
    assert(mgr.objects.size() == 1 && mgr.objects["foo"] == 11);
}

/// 
/// /////////////////////////////////////////////////////////////////////////////////
///

bool MapWithRemoveManager::RemoveObject(const std::string& key)
{
    HISTORY_PUSH(RemoveObject, key);

    // Store custom parameter.
    // WARNING: Variable name is part of its key!!!
    int hOldValue = objects[key];
    HISTORY_SAVE(hOldValue);

    objects.erase(key);
    return true;
}

bool MapWithRemoveManager::RemoveObject_Undo(const std::string& key)
{
    HISTORY_POP();

    // WARNING: Variable type and name must match!!!
    int hOldValue;
    HISTORY_LOAD(hOldValue);

    AddObject(key, hOldValue);
    return true;
}

void HistoryShowcase_UserParams()
{
    MapWithRemoveManager mgr;
    mgr.AddObject("foo", 11);
    mgr.RemoveObject("foo");

    assert(mgr.objects.size() == 0);
    History::GetContext()->Undo();
    assert(mgr.objects.size() == 1 && mgr.objects["foo"] == 11);
    History::GetContext()->Redo();
    assert(mgr.objects.size() == 0);
}

/// 
/// /////////////////////////////////////////////////////////////////////////////////
///

bool MergingManager::SetObject(const std::string& key, const std::set<int>& values)
{
    HISTORY_PUSH(SetObject, key, values);

    // Preserve old values if not inserting.
    if (objects.find(key) != objects.end())
    {
        std::set<int> hOldValues = objects[key];
        HISTORY_SAVE(hOldValues);
    }

    objects[key] = values;
    return true;
}

bool MergingManager::SetObject_Undo(const std::string& key, const std::set<int>& /*unused*/)
{
    HISTORY_POP();

    std::set<int> hOldValues;
    if (HISTORY_LOAD(hOldValues))
    {
        // Loaded old values = undo edittion
        SetObject(key, hOldValues);
    }
    else
    {
        // Failed to load old values = undo addition
        RemoveObject(key);
    }

    return true;
}

bool MergingManager::RemoveObject(const std::string& key)
{
    HISTORY_PUSH(RemoveObject, key);

    auto&& hOldValue = objects[key];
    HISTORY_SAVE(hOldValue);

    objects.erase(key);
    return true;
}

bool MergingManager::RemoveObject_Undo(const std::string& key)
{
    HISTORY_POP();

    std::set<int> hOldValue;
    HISTORY_LOAD(hOldValue);

    SetObject(key, hOldValue);
    return true;
}

bool MergingManager::MergeObjects(const std::set<std::string>& keys, const std::string& newKey)
{
    HISTORY_PUSH(MergeObjects, keys, newKey);
    std::set<int> hNewValues;

    if (!HISTORY_LOAD(hNewValues))
    {
        // If this is the natural execution (not redo), compute and store merged state.
        for (auto&& key : keys)
            for (int value : objects[key])
                hNewValues.insert(value);

        HISTORY_SAVE(hNewValues);
    }

    // Step #1: Remove source values
    for (auto&& key : keys)
        RemoveObject(key);

    // Step #2: Insert merged value
    SetObject(newKey, hNewValues);
    return true;
}

bool MergingManager::MergeObjects_Undo(const std::set<std::string>& keys, const std::string& newKey)
{
    HISTORY_POP();

    // WARNING: Stack unwinding - reverse step order!!!

    // Undo Step #2
    SetObject_Undo(newKey);

    // Undo Step #1 (reverse iteration!)
    for (auto rit = keys.rbegin(); rit != keys.rend(); ++rit)
        RemoveObject_Undo(*rit);

    return true;
}

void HistoryShowcase_Advanced()
{
    MergingManager mgr;
    mgr.SetObject("foo", {11, 23, 49});
    mgr.SetObject("bar", {7, 8, 23});
    mgr.MergeObjects({ "foo", "bar" }, "foobar");

    assert((mgr.objects.size() == 1) && (mgr.objects["foobar"] == std::set<int>{7, 8, 11, 23, 49}));
    History::GetContext()->Undo();
    assert((mgr.objects.size() == 2) && (mgr.objects["foo"] == std::set<int>{11, 23, 49}) && (mgr.objects["bar"] == std::set<int>{7, 8, 23}));
    History::GetContext()->Redo();
    assert((mgr.objects.size() == 1) && (mgr.objects["foobar"] == std::set<int>{7, 8, 11, 23, 49}));
}

int main()
{
    HistoryShowcase_Basics();
    HistoryShowcase_InlineParams();
    HistoryShowcase_UserParams();
    HistoryShowcase_Advanced();
    return 0;
}
