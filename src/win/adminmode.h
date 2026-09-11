#pragma once
// 管理员权限模式：设置页开关 + 运行时提权检测。
// 开启 = 在 Windows 兼容性标记（AppCompatFlags\Layers）里为当前 exe 登记
// "以管理员身份运行"——下次启动时（双击 / 自启动 / 托盘）自动提权；
// UAC 设为「从不通知」的用户会静默提权，其余用户每次启动点一次确认。

namespace app {

// 当前进程是否以管理员（提权）令牌运行
bool RunningElevated();

// 指定进程是否以管理员（提权）令牌运行（0 = 查询失败按普通处理）
bool ProcessIsElevated(unsigned long pid);

// 终结指定进程（需要调用者具有更高/同等权限：提权实例可终结普通权限实例）
bool KillProcess(unsigned long pid);

// 指定进程是否以管理员（提权）令牌运行（0 = 查询失败按普通处理）
bool ProcessIsElevated(unsigned long pid);

// 终结指定进程（需要调用者具有更高/同等权限：提权实例可终结普通权限实例）
bool KillProcess(unsigned long pid);

// 管理员模式是否已开启（读取兼容性标记；含旧注册表 Run 方式的兼容判断由自启动模块负责）
bool AdminModeFlagged();

// 开启/关闭管理员模式：写入/删除兼容性标记。下次启动生效。
// 同时会更新自启动计划任务的权限级别（若自启动已开启）——由 autostart 模块联动。
bool SetAdminModeFlagged(bool enable);

// 以管理员权限重启自己（弹 UAC）：新实例接管后由调用方退出本进程。
// 返回 false = 用户取消了 UAC 或启动失败（本进程继续运行）。
bool RelaunchAsAdmin();

// 终结同 exe 的其它普通权限进程（提权实例接管前的清理）。返回终结数量。
int KillOtherInstances();

} // namespace app
