#include "Chat.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Config.h"
#include "WorldSession.h"
#include "World.h"
#include "WorldSessionMgr.h"
#include "DatabaseEnv.h"
#include <unordered_set>

class JunkToGold : public PlayerScript
{
private:
    // 配置变量
    bool m_Enabled;
    uint8 m_MinQuality;
    uint8 m_MaxQuality;
    bool m_ShowMessage;
    uint8 m_MinLevel;
    bool m_OnlyForBots;
    bool m_ExcludeBOP;
    bool m_ExcludeQuestItems;
    std::unordered_set<uint32> m_Blacklist;
    std::unordered_set<uint32> m_Whitelist;

public:
    JunkToGold() : PlayerScript("JunkToGold")
    {
        // 从配置文件读取所有设置
        ReloadConfig();
        
        if (m_Enabled)
        {
            LOG_INFO("module", "JunkToGold: 已启用");
            LOG_INFO("module", "  - 品质范围: {}-{}", m_MinQuality, m_MaxQuality);
            LOG_INFO("module", "  - 显示消息: {}", m_ShowMessage ? "是" : "否");
            LOG_INFO("module", "  - 最低等级: {}", m_MinLevel);
            LOG_INFO("module", "  - 仅对机器人: {}", m_OnlyForBots ? "是" : "否");
            LOG_INFO("module", "  - 排除拾取绑定: {}", m_ExcludeBOP ? "是" : "否");
            LOG_INFO("module", "  - 排除任务物品: {}", m_ExcludeQuestItems ? "是" : "否");
            LOG_INFO("module", "  - 黑名单物品数: {}", m_Blacklist.size());
            LOG_INFO("module", "  - 白名单物品数: {}", m_Whitelist.size());
        }
        else
        {
            LOG_INFO("module", "JunkToGold: 已禁用");
        }
    }

    void ReloadConfig()
    {
        m_Enabled = sConfigMgr->GetOption<bool>("JunkToGold.Enable", true);
        m_MinQuality = sConfigMgr->GetOption<uint8>("JunkToGold.MinQuality", 0);
        m_MaxQuality = sConfigMgr->GetOption<uint8>("JunkToGold.MaxQuality", 0);
        m_ShowMessage = sConfigMgr->GetOption<bool>("JunkToGold.ShowMessage", false);
        m_MinLevel = sConfigMgr->GetOption<uint8>("JunkToGold.MinLevel", 1);
        m_OnlyForBots = sConfigMgr->GetOption<bool>("JunkToGold.OnlyForBots", true);
        m_ExcludeBOP = sConfigMgr->GetOption<bool>("JunkToGold.ExcludeBOP", true);
        m_ExcludeQuestItems = sConfigMgr->GetOption<bool>("JunkToGold.ExcludeQuestItems", true);
        
        // 解析黑名单
        std::string blacklistStr = sConfigMgr->GetOption<std::string>("JunkToGold.Blacklist", "");
        ParseItemList(blacklistStr, m_Blacklist);
        
        // 解析白名单
        std::string whitelistStr = sConfigMgr->GetOption<std::string>("JunkToGold.Whitelist", "");
        ParseItemList(whitelistStr, m_Whitelist);
    }

    void OnPlayerLootItem(Player* player, Item* item, uint32 count, ObjectGuid /*lootguid*/) override
    {
        // 1. 检查模块是否启用
        if (!m_Enabled)
            return;
            
        // 2. 检查是否为无效物品
        if (!item || !item->GetTemplate())
            return;
            
        // 3. 检查是否只对机器人有效
        if (m_OnlyForBots && !IsBot(player))
            return;
            
        // 4. 检查玩家等级
        if (player->GetLevel() < m_MinLevel)
            return;
            
        // 5. 检查物品模板
        ItemTemplate const* itemTemplate = item->GetTemplate();
        
        // 6. 检查物品品质范围
        uint8 quality = itemTemplate->Quality;
        if (quality < m_MinQuality || quality > m_MaxQuality)
            return;
            
        // 7. 检查黑名单
        if (m_Blacklist.find(itemTemplate->ItemId) != m_Blacklist.end())
            return;
            
        // 8. 检查白名单（如果设置了白名单，则只处理白名单中的物品）
        if (!m_Whitelist.empty() && m_Whitelist.find(itemTemplate->ItemId) == m_Whitelist.end())
            return;
            
        // 9. 检查拾取绑定
        if (m_ExcludeBOP && itemTemplate->Bonding == BIND_WHEN_PICKED_UP)
            return;
            
        // 10. 检查任务物品
        if (m_ExcludeQuestItems && itemTemplate->Class == ITEM_CLASS_QUEST)
            return;
            
        // 11. 检查物品是否可出售（有出售价格）
        if (itemTemplate->SellPrice == 0)
            return;
            
        // 12. 计算总金额
        uint32 totalMoney = itemTemplate->SellPrice * count;
        
        // 13. 发送交易信息（如果启用）
        if (m_ShowMessage)
        {
            SendTransactionInformation(player, itemTemplate, count, totalMoney);
        }
        
        // 14. 给予玩家金币
        player->ModifyMoney(totalMoney);
        
        // 15. 销毁物品
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    }

private:
    // 检测是否为机器人的方法
    bool IsBot(Player* player)
    {
        WorldSession* session = player->GetSession();
        if (!session)
            return true; // 没有会话，判定为机器人
        
        // 获取账号ID
        uint32 accountId = session->GetAccountId();
        
        // 查询account_tutorial表，检查accountId是否存在
        QueryResult result = CharacterDatabase.Query(
            "SELECT 1 FROM account_tutorial WHERE accountId = {}",
            accountId
        );
        
        // 如果查询到结果，说明accountId存在于表中，判定为不是机器人
        if (result)
        {
            LOG_DEBUG("module", "JunkToGold: 账户ID {} 存在于account_tutorial表中，判定为非机器人", accountId);
            return false; // 不是机器人
        }
        
        // 如果查询不到结果，说明accountId不存在于表中，判定为机器人
        LOG_DEBUG("module", "JunkToGold: 账户ID {} 不存在于account_tutorial表中，判定为机器人", accountId);
        return true; // 是机器人
    }

    // 解析物品列表
    void ParseItemList(const std::string& listStr, std::unordered_set<uint32>& itemSet)
    {
        itemSet.clear();
        
        if (listStr.empty())
            return;
            
        std::stringstream ss(listStr);
        std::string itemIdStr;
        
        while (std::getline(ss, itemIdStr, ','))
        {
            // 去除空格
            itemIdStr.erase(0, itemIdStr.find_first_not_of(' '));
            itemIdStr.erase(itemIdStr.find_last_not_of(' ') + 1);
            
            if (!itemIdStr.empty())
            {
                try
                {
                    uint32 itemId = static_cast<uint32>(std::stoul(itemIdStr));
                    itemSet.insert(itemId);
                }
                catch (const std::exception&)
                {
                    LOG_ERROR("module", "JunkToGold: 无效的物品ID: {}", itemIdStr);
                }
            }
        }
    }

    // 发送交易信息
    void SendTransactionInformation(Player* player, ItemTemplate const* itemTemplate, uint32 count, uint32 totalMoney)
    {
        std::string name;
        if (count > 1)
        {
            name = Acore::StringFormat("|cff9d9d9d|Hitem:{}::::::::80:::::|h[{}]|h|rx{}", 
                itemTemplate->ItemId, itemTemplate->Name1, count);
        }
        else
        {
            name = Acore::StringFormat("|cff9d9d9d|Hitem:{}::::::::80:::::|h[{}]|h|r", 
                itemTemplate->ItemId, itemTemplate->Name1);
        }

        uint32 gold = totalMoney / GOLD;
        uint32 silver = (totalMoney % GOLD) / SILVER;
        uint32 copper = totalMoney % SILVER;

        std::string info;
        if (totalMoney < SILVER)
        {
            info = Acore::StringFormat("{} 出售获得 {} 铜币。", name, copper);
        }
        else if (totalMoney < GOLD)
        {
            if (copper > 0)
            {
                info = Acore::StringFormat("{} 出售获得 {} 银币和 {} 铜币。", name, silver, copper);
            }
            else
            {
                info = Acore::StringFormat("{} 出售获得 {} 银币。", name, silver);
            }
        }
        else
        {
            if (copper > 0 && silver > 0)
            {
                info = Acore::StringFormat("{} 出售获得 {} 金币, {} 银币和 {} 铜币。", name, gold, silver, copper);
            }
            else if (copper > 0)
            {
                info = Acore::StringFormat("{} 出售获得 {} 金币和 {} 铜币。", name, gold, copper);
            }
            else if (silver > 0)
            {
                info = Acore::StringFormat("{} 出售获得 {} 金币和 {} 银币。", name, gold, silver);
            }
            else
            {
                info = Acore::StringFormat("{} 出售获得 {} 金币。", name, gold);
            }
        }

        // 方式1：仅自己可见
        // ChatHandler(player->GetSession()).SendSysMessage(info);
        
        // 方式2：服务器范围的公告（所有在线玩家都会看到）
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, info.c_str());
    }
};

void Addmod_junk_to_goldScripts()
{
    new JunkToGold();
}
