#ifndef DIAG_CLI_H
#define DIAG_CLI_H

/* USART6（PC6 TX / PC7 RX，115200 8N1）上的诊断命令行。日常保持静默，只在收到命令时输出。
 * 命令和报告格式见 docs/diagnostics.md。中断里只收发字节，解析和格式化都在低优先级的
 * CLI 任务里，输出拥堵时只让 CLI 任务等待，不影响 GPS、CAN 和显示。 */

#include <stdint.h>

#define DIAG_REPORT_FORMAT  1U

/* 创建 CLI 任务并启动 USART6 接收。在调度器启动前调用（MX_FREERTOS_Init）。 */
void Diag_CLI_Init(void);

/* 任务循环体：处理收到的字符、推进当前命令、启动发送。无阻塞，now 为 HAL_GetTick()。 */
void Diag_CLI_Step(uint32_t now);

/* HAL 回调转发（中断上下文）。 */
void Diag_CLI_UartRxCplt(void);
void Diag_CLI_UartTxDone(void);
void Diag_CLI_UartError(uint32_t error_code);

#endif
