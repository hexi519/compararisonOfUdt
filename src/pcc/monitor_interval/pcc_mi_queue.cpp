#ifdef QUIC_PORT
#ifdef QUIC_PORT_LOCAL
#include "net/quic/core/congestion_control/pcc_monitor_interval_queue.h"
#include "net/quic/core/congestion_control/rtt_stats.h"
#else
#include "third_party/pcc_quic/pcc_monitor_interval_queue.h"
#include "gfe/quic/core/congestion_control/rtt_stats.h"
#endif
#else
#include "pcc_mi_queue.h"
#include <iostream>
#endif

#ifdef QUIC_PORT
#ifdef QUIC_PORT_LOCAL
namespace net
{
#else
namespace gfe_quic
{
#endif
#endif

#ifndef QUIC_PORT
//#define DEBUG_UTILITY_CALC
#define DEBUG_MONITOR_INTERVAL_QUEUE_ACKS
#define DEBUG_MONITOR_INTERVAL_QUEUE_LOSS
#define DEBUG_INTERVAL_SIZE

#endif

    namespace
    {
        // Number of probing MonitorIntervals necessary for Probing.
        //const size_t kRoundsPerProbing = 4;
        // Tolerance of loss rate by utility function.
        const float kLossTolerance = 0.05f;
        // Coefficeint of the loss rate term in utility function.
        const float kLossCoefficient = -1000.0f;
        // Coefficient of RTT term in utility function.
        const float kRTTCoefficient = -200.0f;
        // Coefficienty of the latency term in the utility function.
        const float kLatencyCoefficient = 1;
        // Alpha factor in the utility function.
        const float kAlpha = 1;
        // An exponent in the utility function.  // used in utility calculations ,duplicate codes
        const float kExponent = 0.9;
        // An exponent in the utility function.
        const size_t kMegabit = 1024 * 1024;
#ifndef QUIC_PORT_LOCAL
        // Number of microseconds per second.
        const float kNumMicrosPerSecond = 1000000.0f;
#endif
    } // namespace

    PccMonitorIntervalQueue::PccMonitorIntervalQueue() {}

#if defined(QUIC_PORT) && defined(QUIC_PORT_LOCAL)
    PccMonitorIntervalQueue::~PccMonitorIntervalQueue()
    {
    }
#endif
    // push at back
    void PccMonitorIntervalQueue::Push(MonitorInterval mi)
    {
        monitor_intervals_.emplace_back(mi);
    }

    // poip from front
    MonitorInterval PccMonitorIntervalQueue::Pop()
    {
        MonitorInterval mi = monitor_intervals_.front();
        monitor_intervals_.pop_front();
        return mi;
    }

    // finish判断准则：是否这个MI内发出的所有包的着落信息都已收集到
    bool PccMonitorIntervalQueue::HasFinishedInterval(QuicTime cur_time)
    {
        if (monitor_intervals_.empty())
        {
            return false;
        }
        return monitor_intervals_.front().AllPacketsAccountedFor(cur_time);
    }

    // 调用最新的(back的)MI的OnPacketSent
    void PccMonitorIntervalQueue::OnPacketSent(QuicTime sent_time,
                                               QuicPacketNumber packet_number,
                                               QuicByteCount bytes)
    {
        if (monitor_intervals_.empty())
        {
#ifdef QUIC_PORT
            QUIC_BUG << "OnPacketSent called with empty queue.";
#endif
            return;
        }

        MonitorInterval &interval = monitor_intervals_.back();
        interval.OnPacketSent(sent_time, packet_number, bytes);
    }

    // 遍历到还没有utility的MI，调用他们的 OnPacketAcked & OnPacketLost
    //??疑惑点：有情况会一次性传入多个ACK?
    void PccMonitorIntervalQueue::OnCongestionEvent(
        const AckedPacketVector &acked_packets,
        const LostPacketVector &lost_packets,
        int64_t rtt_us,
        QuicTime event_time)
    {

        // 讲道理 应该不会  或者说这个情况很少发生，因为上层里面已经针对没有MI的情况下进行了补充：createNewMI
        if (monitor_intervals_.empty())
        { //! hesy:所以不在MI范围的时候，是不需要改变的，保持就行-> 这不是问题很大？ 不灵敏啊？ 除非一直在MI期间？？
            // Skip all the received packets if we have no monitor intervals.
            std::cerr << "hesy:OnCongestionEvent in PccMIQueue: queue is empty" << std::endl;
            return;
        }

        for (MonitorInterval &interval : monitor_intervals_)
        {
            if (interval.AllPacketsAccountedFor(event_time))
            {
                // Skips intervals that have available utilities.
                continue;
            }

            for (const AckedPacket &acked_packet : acked_packets)
            {
                interval.OnPacketAcked(event_time, acked_packet.packet_number, acked_packet.bytes_acked, rtt_us);
            }

            for (const LostPacket &lost_packet : lost_packets)
            {
                interval.OnPacketLost(event_time, lost_packet.packet_number, lost_packet.bytes_lost);
            }
        }
    }

    // 取出队尾的元素
    const MonitorInterval &PccMonitorIntervalQueue::Current() const
    {
#ifdef QUIC_PORT
        DCHECK(!monitor_intervals_.empty());
#endif
        return monitor_intervals_.back();
    }

    // done
    bool PccMonitorIntervalQueue::Empty() const
    {
        return monitor_intervals_.empty();
    }

#ifndef QUIC_PORT_LOCAL
    // done
    size_t PccMonitorIntervalQueue::Size() const
    {
        return monitor_intervals_.size();
    }

#endif

#ifdef QUIC_PORT
} // namespace gfe_quic
#endif
