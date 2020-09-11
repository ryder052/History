#pragma once
#include <vector>
#include <map>
#include <set>

void HistoryShowcase_Basics();
void HistoryShowcase_InlineParams();
void HistoryShowcase_UserParams();
void HistoryShowcase_Advanced();

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

struct MapManager : ManagerBase
{
    std::map<std::string, int> objects;

    bool AddObject(const std::string& key, int value = 0);
    bool AddObject_Undo(const std::string& key, int value = 0);
};

struct MapWithRemoveManager : MapManager
{
    bool RemoveObject(const std::string& key);
    bool RemoveObject_Undo(const std::string& key);
};

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