#include <onix/net/tcp.h>
#include <onix/list.h>
#include <onix/task.h>
#include <onix/assert.h>
#include <onix/debug.h>

#define LOGK(fmt, args...) DEBUGK(fmt, ##args)

extern list_t tcp_pcb_active_list;   // 活动 pcb 列表
extern list_t tcp_pcb_timewait_list; // 等待 pcb 列表

static task_t *tcp_slow; // TCP 慢速任务
static task_t *tcp_fast; // TCP 快速任务

static void tcp_fastimo()
{
    list_t *lists[] = {&tcp_pcb_active_list, &tcp_pcb_timewait_list};
    while (true)
    {
        for (size_t i = 0; i < 2; i++)
        {
            list_t *list = lists[i];
            for (list_node_t *node = list->head.next; node != &list->tail; node = node->next)
            {
                tcp_pcb_t *pcb = element_entry(tcp_pcb_t, node, node);
                if (pcb->flags & TF_ACK_DELAY)
                    pcb->flags |= TF_ACK_NOW;
                if (pcb->flags & TF_ACK_NOW)
                    tcp_output(pcb);
            }
        }
        task_sleep(TCP_FAST_INTERVAL);
    }
}

static void tcp_timeout(tcp_pcb_t *pcb, int type)
{
    switch (type)
    {
    case TCP_TIMER_SYN:
        LOGK("tcp timeout syn\n");
        if (pcb->state != SYN_SENT && pcb->state != SYN_RCVD)
            return;
        tcp_pcb_purge(pcb, -ETIME);
        pcb->state = CLOSED;
        break;
    case TCP_TIMER_REXMIT:
        LOGK("tcp timeout rexmit\n");
        pcb->rtx_cnt++;
        if (pcb->rtx_cnt > TCP_MAXRXTCNT)
        {
            tcp_pcb_purge(pcb, -ETIME);
            pcb->state = CLOSED;
            return;
        }
        tcp_rexmit(pcb);
        break;
    case TCP_TIMER_PERSIST:
        break;
    case TCP_TIMER_KEEPALIVE:
        break;
    case TCP_TIMER_FIN_WAIT2:
    case TCP_TIMER_TIMEWAIT:
        tcp_pcb_put(pcb);
        break;
    default:
        break;
    }
}

static void tcp_slowtimo()
{
    list_t *lists[] = {&tcp_pcb_active_list, &tcp_pcb_timewait_list};
    while (true)
    {
        for (size_t i = 0; i < 2; i++)
        {
            list_t *list = lists[i];
            for (list_node_t *node = list->head.next; node != &list->tail;)
            {
                tcp_pcb_t *pcb = element_entry(tcp_pcb_t, node, node);
                node = node->next;

                pcb->idle++;
                for (size_t i = 0; i < TCP_TIMER_NUM; i++)
                {
                    if (pcb->timers[i] && (--pcb->timers[i]) == 0)
                    {
                        tcp_timeout(pcb, i);
                    }
                }
            }
        }
        task_sleep(TCP_SLOW_INTERVAL);
    }
}

void tcp_timer_init()
{
    tcp_fast = task_create(tcp_fastimo, "tcp_fast", 5, KERNEL_USER);
    tcp_slow = task_create(tcp_slowtimo, "tcp_slow", 5, KERNEL_USER);
}