# History
Hello Developers!

I present to you History, a modern C++ (C++17) Undo / Redo framework. My goal was to create a non-intrusive, compact and intuitive solution.
Let's dive straight into it.

## Example 1: The Basics
*Showcase.h*
```C++
struct ManagerBase
{
    ManagerBase();
    HistoryContext context;
};

struct TrivialManager : ManagerBase
{
    std::vector<int> objects;

    bool AddNewObject();
    bool AddNewObject_Undo();
};
```

*Showcase.cpp*
```C++
ManagerBase::ManagerBase()
{
    History::SetContext(&context);
}

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
```
A couple of things happening here:
* `History::SetContext(...)` basically enables all History. You can have any number of separate contexts / stacks, just manage the switching yourself;
* `HISTORY_PUSH(...)` is the main macro that creates a History record on the stack. 
  - You pass the name of the method as the first parameter;
  - It forces creation of a mirror method with a "_Undo" suffix; 
  - Redo function is the same exact function you're calling `HISTORY_PUSH(...)` from!
  - This macro is meant for member functions - for free functions use `HISTORY_PUSH_FREE(...)`;
  - All functions used in History must return a boolean;
* `HISTORY_POP()` must be called at the beginning of each xxx_Undo() function.

## Example 2: Inline parameters
*Showcase.h*
```C++
struct MapManager : ManagerBase
{
    std::map<std::string, int> objects;

    bool AddObject(const std::string& key, int value = 0);
    bool AddObject_Undo(const std::string& key, int value = 0);
};
```

*Showcase.cpp*
```C++
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
```
The only difference from the previous example is the storage of inline function parameters.
`HISTORY_PUSH()` takes them all. 
You don't have to pass those values exactly - if you know you don't need them during undo/redo, passing a default-constructed object is viable.

## Example 3: Custom mementos
*Showcase.h*
```C++
struct MapWithRemoveManager : MapManager
{
    bool RemoveObject(const std::string& key);
    bool RemoveObject_Undo(const std::string& key);
};
```

*Showcase.cpp*
```C++
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
```
Now we're saving anything we want into the History stack record.
`HISTORY_SAVE(...)` does exactly that: It saves whatever you pass into it.

`HISTORY_LOAD(...)` retrieves it later during undo and/or redo. It returns a boolean - true mean the load was successfull.

`HISTORY_SAVEN` and `HISTORY_LOADN` are provided to store / load up to 4 objects at once.

`HISTORY_SAVE_UNSAFE` and `HISTORY_LOAD_UNSAFE` are provided if you *really* want to save into / load from the top of the History stack somewhere other than the main function.

You can call any "Do" functions from "Undo" functions :)

**You should not call "Undo" functions from "Do" functions.**

## Example 4: Advanced usage
*Showcase.h*
```C++
struct MergingManager : ManagerBase
{
    std::map<std::string, std::set<int>> objects;

    bool SetObject(const std::string& key, const std::set<int>& values = {});
    bool SetObject_Undo(const std::string& key, const std::set<int>& values = {});

    bool RemoveObject(const std::string& key);
    bool RemoveObject_Undo(const std::string& key);

    bool MergeObjects(const std::set<std::string>& keys, const std::string& newKey);
    bool MergeObjects_Undo(const std::set<std::string>& keys, const std::string& newKey);
};
```

*Showcase.cpp*
```C++
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
}```

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
```
SetObject can either add a new object or modify an existing one. 
In the first case we don't save the previous value.
It matters during Undo, as `HISTORY_LOAD`'s result makes for a clean logical branch.

```C++
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
```
Only value type's changed from the previous example.

```C++
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
```
The real fun starts here.
This method is used for both "Firstdo" and "Redo".
If it's the "Firstdo" (Load failed), compute the merged state and save it. 
If it's the "Redo", just load the merged state.
Then we remove all source keys and values and insert the merged object.
Have you noticed that each submethod used here also uses `HISTORY_PUSH`?

```C++
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
```
The fun continues!
Because we actually used submethods with `HISTORY_PUSH`, now we can simply unwind the 'substack'!

The rule is: **Either unwind the whole substack using XXX_Undo methods, OR don't use XXX_Undo at all**. No middle ground, or it will break.

## Summary
- History::SetContext() first ;)
- `HISTORY_PUSH` creates a record on the undo stack
- **Call `HISTORY_POP` at the beginning of each Undo function**
- Use `HISTORY_SAVE` and `HISORY_LOAD` to store custom mementos
- Mix it all up freely, except:
  - **Do not call Undo functions from Do functions**
  - **Either unwind the whole substack using XXX_Undo methods, OR don't use XXX_Undo at all**
  
Happy coding!
