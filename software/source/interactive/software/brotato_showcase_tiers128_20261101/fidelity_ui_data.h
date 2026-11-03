#ifndef FIDELITY_UI_DATA_H
#define FIDELITY_UI_DATA_H
#include "fidelity_visual_data.h"
#define BT_UI_TRANSPARENT_KEY 227u
#define BT_UI_GLYPH_COUNT 472u
extern const uint16_t bt_ui_codes[BT_UI_GLYPH_COUNT];
extern const BtSprite bt_ui_glyphs[BT_UI_GLYPH_COUNT];
extern const BtSprite bt_ui_tinted_glyphs[2][BT_UI_GLYPH_COUNT];
#define BT_UI_ROLE_GLYPH_COUNT 96u
extern const uint16_t bt_ui_role_codes[BT_UI_ROLE_GLYPH_COUNT];
extern const BtSprite bt_ui_role_glyphs[3][BT_UI_ROLE_GLYPH_COUNT];
extern const int8_t bt_ui_role_effect_icons[5][5];
extern const char *const bt_ui_role_effects[5][5];
#define BT_UI_BIG_COUNT 41u
extern const uint16_t bt_ui_big_codes[BT_UI_BIG_COUNT];
extern const BtSprite bt_ui_big_glyphs[BT_UI_BIG_COUNT];
extern const char *const bt_ui_weapon_names[13];
extern const char *const bt_ui_item_names[39];
extern const char *const bt_ui_role_names[5];
extern const char *const bt_ui_stat_names[24];
#define UI_TITLE "土豆兄弟"
#define UI_SOLO "单人游戏"
#define UI_CHARACTER "角色选择"
#define UI_WEAPON "武器选择"
#define UI_DIFFICULTY "危险等级"
#define UI_ENDLESS "无尽模式"
#define UI_START "开始"
#define UI_ON "开启"
#define UI_OFF "关闭"
#define UI_SHOP "商店"
#define UI_STATS "属性"
#define UI_WEAPONS "武器"
#define UI_ITEMS "道具"
#define UI_MATERIALS "材料"
#define UI_BAG "储存材料"
#define UI_LEVEL "等级"
#define UI_WAVE "波次"
#define UI_TIME "剩余时间"
#define UI_HEALTH "生命"
#define UI_REROLL "刷新"
#define UI_NEXT_WAVE "下一波"
#define UI_SOLD "已售出"
#define UI_LOCKED "已锁定"
#define UI_TIER "品质"
#define UI_DAMAGE "伤害"
#define UI_COOLDOWN "冷却"
#define UI_RANGE "范围"
#define UI_SECONDS "秒"
#define UI_SETS "套装"
#define UI_UPGRADE "选择一项升级"
#define UI_UPGRADE_HINT "左右选择  下选刷新  A确认"
#define UI_CRATE "战利品"
#define UI_TAKE "拿取"
#define UI_RECYCLE "回收"
#define UI_PAUSE "暂停 / 选项"
#define UI_RESUME "继续"
#define UI_VOLUME "音量"
#define UI_MUTED "静音"
#define UI_AUDIO_ON "声音开启"
#define UI_WIN "胜利"
#define UI_DEAD "本轮结束"
#define UI_KILLS "击杀"
#define UI_RESTART "新游戏"
#define UI_HOME "返回主菜单"
#define UI_FAULT "运行已停止"
#define UI_FAULT_CODE "故障代码"
#define UI_TEST_GOD "测试无敌"
#define UI_SELECT_HINT "方向键选择  A确认  B返回"
#define UI_SETUP_HINT "左右选择  A继续  B返回"
#define UI_START_HINT "A确认  Start选项  Select返回"
#define UI_GOD_HINT "同时按L和R切换测试无敌"
#define UI_START_GAME "开始游戏"
#define UI_HOME_HINT "上下选择  A确认  Select返回主菜单"
#define UI_SFX_MUTED "声音已静音  X开启"
#define UI_SFX_ON "声音已开启  X静音"
#define UI_SHOP_HINT "A购买/武器操作 X锁定 Y刷新"
#define UI_NEXT_HINT "上下:刷新/商品/武器/下一波 左右选择"
#define UI_COMBINE "合成"
#define UI_CANCEL "取消"
#define UI_NO_WEAPONS "没有武器"
#define UI_POPUP_HINT "上下选择 A确认 B取消"
#define UI_PAUSE_HINT "A或Start继续  X静音  左右调音量"
#define UI_CLOSE_HINT "B或A关闭"
#define UI_CRATE_HINT "A拿取  B回收"
#define UI_END_HINT "A新游戏  Select返回主菜单"
#define UI_LOW_MATERIALS "材料不足"
#define UI_WEAPONS_FULL "武器已满"
#define UI_NO_PAIR "没有同阶同类武器"
#define UI_ITEM_LIMIT "道具达到上限"
#define UI_ALL_LOCKED "商品全部锁定"
#define UI_CLASSIC "经典模式"
#define UI_ROLE_WELL "最大生命值+5  速度+5%  收获+8"
#define UI_ROLE_RANGER "远程伤害收益+50%  范围+50"
#define UI_ROLE_RANGER_BAD "最大生命值收益减少25%  仅远程武器"
#define UI_ROLE_BRAWLER "徒手武器攻击速度+50%  闪避+15%"
#define UI_ROLE_BRAWLER_BAD "范围-50  远程伤害-50"
#define UI_ROLE_ENGINEER "工程学+10  工程学收益+25%"
#define UI_ROLE_ENGINEER_BAD "伤害属性收益减少50%  初始额外扳手"
#define UI_ROLE_ENGINEER_MORE "炮塔集中生成  工程学增强结构伤害"
#define UI_ROLE_PACIFIST "存活敌人数的65%奖励材料与经验  一击砍树"
#define UI_ROLE_PACIFIST_BAD "伤害-100%  工程学-100"
#define UI_ROLE_PACIFIST_MORE "掌提供击退和收获  六掌构筑保命成长"
#define UI_HAND_INFO "击退敌人并提高收获"
#define UI_TASER_INFO "区域减速  元素伤害加成"
#define UI_PRUNER_INFO "生成花园  定期产出治疗水果"
#define UI_WRENCH_INFO "每波生成对应品质炮塔"
#define UI_SCREWDRIVER_INFO "定期生成地雷  近战和工程学加成"
#define UI_SHIRT_INFO "一击砍倒树木"
#define UI_TURRET_INFO "每波生成一个炮塔"
#define UI_MINE_INFO "每12秒生成地雷"
#define UI_ENGINEERING_INFO "伤害随工程学提高"
#define UI_BURN_INFO "每半秒燃烧一次  共8次"
#define UI_MINE_TRIGGER "踩上后离开触发爆炸"
#define UI_HEALING_INFO "发射治疗弹治疗玩家"
#define UI_EXPLOSION_INFO "发射爆炸弹"
#define UI_PIERCING_INFO "投射物可穿透敌人"
#define UI_HEAL "治疗"
#define UI_BURN_DAMAGE "燃烧伤害"
#define UI_CONFIRM "确认构筑"
#define UI_SINGLE_MAP "坠落之地"
#define UI_DANGER0 "没有额外难度"
#define UI_DANGER1 "出现新的敌人"
#define UI_DANGER2 "出现精英与虫群"
#define UI_DANGER3 "敌人生命与伤害+12%"
#define UI_DANGER4 "敌人生命与伤害+26%"
#define UI_DANGER5 "敌人生命与伤害+40%  最终波出现两个头目"
#define UI_DANGER_INHERIT "包含较低危险等级的变化"
#define UI_SHOWCASE "性能展示"
#define UI_SHOWCASE_BUILD "固定和平主义者 六手掌 危险0"
#define UI_SHOWCASE_HINT "上下选择性能展示  A确认"
#define UI_SHOWCASE_RULES "每波10秒 自动继续 正常伤害 第三段保持负载"
extern const BtSprite bt_ui_weapon_icons[13];
extern const BtSprite bt_ui_item_icons[39];
extern const BtSprite bt_ui_role_icons[5];
extern const BtSprite bt_ui_stat_icons[24];
extern const BtSprite bt_ui_stat_large_icons[24];
extern const BtSprite bt_ui_item_small_icons[39];
extern const BtSprite bt_ui_danger_icons[6];
extern const BtSprite bt_ui_stat_medium_icons[24];
extern const BtSprite bt_ui_role_large_icons[5];
extern const BtSprite bt_ui_backdrop;
#endif
