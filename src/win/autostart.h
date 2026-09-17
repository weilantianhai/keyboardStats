#pragma once
// 开机自启动：计划任务（登录触发 + 最高权限）。
//
// 设计要点（每一条都是踩过坑的结论）：
//  1. 记录进程必须是高完整性：管理员窗口、反作弊游戏（Valorant/原神启动器）内
//     才能收到输入（普通权限钩子会被 UIPI 屏蔽）。任务计划程序服务可以授予
//     最高权限且不弹 UAC —— 这是唯一可靠的开机提权启动方式。
//  2. 任务固定 RunLevel=Highest。曾出现过的失败组合：任务 LeastPrivilege +
//     exe 带 RUNASADMIN 兼容标记 —— 任务以普通权限启动时兼容层要求提权，
//     而它没有 UAC 通道，启动被静默拒绝（任务在、却从不运行）。
//  3. 创建 Highest 任务需要提权环境：普通权限下开启开关返回 NeedElevation，
//     由 UI 触发一次静默提权重启后完成创建。
//  4. 用 XML 注册而非 schtasks /TR 参数：/TR 对内嵌引号有转义坑，XML 的
//     Command/Arguments 是元素文本，无转义问题。
//  5. 渲染路径红线：AutostartEnabled 只读任务定义文件并解析权限级别，
//     绝不启动子进程（早期版本在渲染路径里 spawn schtasks 导致设置页卡死）。
#include <string>

std::wstring ExePath();

enum class AutostartResult {
    Ok,             // 已创建 / 已关闭
    NeedElevation,  // 需要管理员权限才能创建最高权限任务，UI 应触发提权重启
    Failed,         // 其它失败
};

// 是否已开启：任务存在且权限级别为 Highest（结果缓存 3 秒，渲染路径安全）。
bool AutostartEnabled();

// 开启：创建/重建最高权限任务。未提权时返回 NeedElevation（不做降级——
// 降级产物与提权标记冲突，会导致开机不启动）。
AutostartResult AutostartEnable();

// 关闭：删除任务。
bool AutostartDisable();

// 修复存量任务：任务存在但权限级别不是 Highest 时重建（提权实例启动时调用，幂等）。
// 用于自动修复"任务在却从不运行"的历史遗留状态。
void AutostartRepairIfNeeded();
