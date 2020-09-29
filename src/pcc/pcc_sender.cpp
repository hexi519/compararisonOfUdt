#ifdef QUIC_PORT
#ifdef QUIC_PORT_LOCAL
#include "net/quic/core/congestion_control/pcc_sender.h"
#include "net/quic/core/congestion_control/rtt_stats.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_str_cat.h"
#else
#include "third_party/pcc_quic/pcc_sender.h"
#include "/quic/src/core/congestion_control/rtt_stats.h"
#include "/quic/src/net/platform/api/quic_str_cat.h"
#include "base_commandlineflags.h"
#endif
#else
#include "pcc_sender.h"
#include "stdlib.h"
#include <random>
#endif

#ifdef QUIC_PORT
#define UDT_UNUSED
#else
#define UDT_UNUSED __attribute__((unused))
#endif

#include <algorithm>

#ifdef QUIC_PORT
#ifdef QUIC_PORT_LOCAL
namespace net {

#else
namespace gfe_quic {

#endif
#endif

namespace {
// Number of bits per Mbit.
const size_t kMegabit = 1024 * 1024;
// Minimum sending rate of the connection.
#ifdef QUIC_PORT
const QuicBandwidth kMinSendingRate = QuicBandwidth::FromKBitsPerSecond(2000);
// The smallest amount that the rate can be changed by at a time.
const QuicBandwidth kMinimumRateChange = QuicBandwidth::FromBitsPerSecond(
    static_cast<int64_t>(0.5f * kMegabit));
#else
// us与s之间的转换率
const float kNumMicrosPerSecond = 1000000.0f;
// Default TCPMSS used in UDT only.
const size_t kDefaultTCPMSS = 1400;
// An inital RTT value to use (10ms)  //? 10ms不应该是 10,000么..
const size_t kInitialRttMicroseconds = 1 * 1000;
#endif
// Number of bits per byte.
const size_t kBitsPerByte = 8;
// Duration of monitor intervals as a proportion of RTT.
//* 之前设置的值是1.5个RTT，现在改成0.5个了...似乎是for training
const float kMonitorIntervalDuration = 0.5f;
// Minimum number of packets in a monitor interval.
const size_t kMinimumPacketsPerInterval = 5;
}  // namespace

#ifdef QUIC_PORT
QuicTime::Delta PccSender::ComputeMonitorDuration(
    QuicBandwidth sending_rate, 
    QuicTime::Delta rtt) {

  return QuicTime::Delta::FromMicroseconds(
      std::max(1.5 * rtt.ToMicroseconds(), 
               kNumMicrosPerSecond * kMinimumPacketsPerInterval * kBitsPerByte * 
                   kDefaultTCPMSS / static_cast<float>(
                       sending_rate.ToBitsPerSecond())));
}
#else
// 有一个最小的MI时间保障，同时还要保障上一个MI时段的包全部发完
QuicTime PccSender::ComputeMonitorDuration(
    QuicBandwidth sending_rate,    // actually just double
    QuicTime rtt) {     // int64_t 
  return
      std::max(kMonitorIntervalDuration * rtt, 
               kNumMicrosPerSecond * kMinimumPacketsPerInterval * kBitsPerByte * 
                   kDefaultTCPMSS / (float)sending_rate);   
                   // 发包个数*包间隔时间*包最小的字节数大小*字节和比特的转换率大小
}
#endif

#ifdef QUIC_PORT
PccSender::PccSender(const RttStats* rtt_stats,
                     QuicPacketCount initial_congestion_window,
                     QuicPacketCount max_congestion_window,
                     QuicRandom* random)
#else
PccSender::PccSender(QuicTime initial_rtt_us,
                     QuicPacketCount initial_congestion_window,    // int32_t
                     UDT_UNUSED QuicPacketCount max_congestion_window)
#endif
    :
#ifdef QUIC_PORT
      sending_rate_(QuicBandwidth::FromBitsPerSecond(
          initial_congestion_window * kDefaultTCPMSS * kBitsPerByte *
          kNumMicrosPerSecond / rtt_stats->initial_rtt_us())),
#else
      sending_rate_(
          initial_congestion_window * kDefaultTCPMSS * kBitsPerByte *
          kNumMicrosPerSecond / initial_rtt_us),
#endif
      interval_analysis_group_(3),  //TODO 怎么size是3...不应该是3么...
      #ifndef QUIC_PORT
      avg_rtt_(0)
      #endif
      {

  std::cout << "Starting sending rate = " << sending_rate_ << std::endl;
  #ifndef QUIC_PORT
  // TODO log的用法
  if (Options::Get("-log=") == NULL) {
    log = new PccEventLogger("pcc_log.txt");
  } else {
    log = new PccEventLogger(Options::Get("-log="));    
  }
  #endif
  
  // CLARG: "--pcc-utility-calc=<utility_calculator>" See src/pcc/utility for more info.
  const char* uc_name = Options::Get("--pcc-utility-calc=");
  if (uc_name == NULL) {
      utility_calculator_ = PccUtilityCalculatorFactory::Create("", log);
  } else {
      utility_calculator_ = PccUtilityCalculatorFactory::Create(std::string(uc_name), log);
  }

  // We'll tell the rate controller how many times per RTT it is called so it can run aglorithms
  // like doubling every RTT fairly easily.
  double call_freq = 1.0 / kMonitorIntervalDuration;    // 每个Mi, rate controller都会被call一次

  // CLARG: "--pcc-rate-control=<rate_controller>" See src/pcc/rate_controler for more info.
  const char* rc_name = Options::Get("--pcc-rate-control=");
  std::string rc_name_str = "";
  if (rc_name != NULL) {
      rc_name_str = std::string(rc_name);
  }
  rate_controller_ = PccRateControllerFactory::Create(rc_name_str, call_freq, log);
    rate_control_lock_ = new std::mutex();
}

#ifndef QUIC_PORT
PccSender::~PccSender() {
    delete log;
    delete utility_calculator_;
    delete rate_controller_;
    delete rate_control_lock_;
}

#endif
#if defined(QUIC_PORT) && defined(QUIC_PORT_LOCAL)
PccSender::~PccSender() {}


#endif
#ifndef QUIC_PORT
void PccSender::Reset() {
    rate_control_lock_->lock();
    rate_controller_->Reset();
    rate_control_lock_->unlock();
}
#endif

// 如果MI_queue空了，或者最新的MI的包全部发完了，就得创建一个新的了
bool PccSender::ShouldCreateNewMonitorInterval(QuicTime sent_time) {
    return interval_queue_.Empty() ||
        interval_queue_.Current().AllPacketsSent(sent_time);    
}

// 更新rtt onCongestionEvent(Ack
void PccSender::UpdateCurrentRttEstimate(QuicTime rtt) {
    #ifdef QUIC_PORT
    return;
    #else
    avg_rtt_ = rtt;
    #endif
}

//done
QuicTime PccSender::GetCurrentRttEstimate(QuicTime sent_time) {
    #ifdef QUIC_PORT
    return rtt_stats_->smoothed_rtt();
    #else
    return avg_rtt_;
    #endif
}

//done
QuicBandwidth PccSender::UpdateSendingRate(QuicTime event_time) {
    rate_control_lock_->lock(); 
    sending_rate_ = rate_controller_->GetNextSendingRate(sending_rate_, event_time);  // 调用python端的，needs reading
    rate_control_lock_->unlock();
    //std::cout << "PCC: rate = " << sending_rate_ << std::endl;
    return sending_rate_;
}

//sent_time 就是当前的时间
void PccSender::OnPacketSent(QuicTime sent_time,
                             UDT_UNUSED QuicByteCount bytes_in_flight,
                             QuicPacketNumber packet_number,
                             QuicByteCount bytes,
                             UDT_UNUSED HasRetransmittableData is_retransmittable) {

  // 现在要发包了，如果不在MI范围内，就创建一个MI
  if (ShouldCreateNewMonitorInterval(sent_time)) {
    // Set the monitor duration to 1.5 of smoothed rtt.
    QuicTime rtt_estimate = GetCurrentRttEstimate(sent_time);
    float sending_rate = UpdateSendingRate(sent_time);
    QuicTime monitor_duration = ComputeMonitorDuration(sending_rate, rtt_estimate); 
    //std::cerr << "Create MI:" << std::endl;
    //std::cerr << "\tTime: " << sent_time << std::endl;
    //std::cerr << "\tPacket Number: " << packet_number << std::endl;
    //std::cerr << "\tDuration: " << monitor_duration << std::endl;
    interval_queue_.Push(MonitorInterval(sending_rate, sent_time + monitor_duration));
    
    #if defined(QUIC_PORT) && defined(QUIC_PORT_LOCAL)
    printf("S %d | st=%d r=%6.3lf rtt=%7ld\n",
           is_useful, mode_,
           interval_queue_.current().sending_rate.ToKBitsPerSecond() / 1000.0,
           rtt_stats_->smoothed_rtt().ToMicroseconds());
    #endif
  }
  interval_queue_.OnPacketSent(sent_time, packet_number, bytes);
}

/*收到一个ack
    1. 更新下rtt_esti(其实就是当前测的值，但是毕竟是测出来的，所以是esti)
    2. 触发MI结构的OnCongestionEvent用于记录消息
    3. 如果一个MI结束了，就计算下其utility，作为一个采样传递给python端，并pop出来
*/
void PccSender::OnCongestionEvent(UDT_UNUSED bool rtt_updated,
                                  UDT_UNUSED QuicByteCount bytes_in_flight,
                                  QuicTime event_time,
  #ifndef QUIC_PORT
                                  QuicTime rtt,
  #endif
                                  const AckedPacketVector& acked_packets,
                                  const LostPacketVector& lost_packets) {
  #ifndef QUIC_PORT
  if (rtt != 0) {
    UpdateCurrentRttEstimate(rtt);
  }
  #endif
  int64_t rtt_estimate = GetCurrentRttEstimate(event_time); 
  
  interval_queue_.OnCongestionEvent(acked_packets, 
                                    lost_packets,
                                    rtt_estimate, 
                                    event_time);
  while (interval_queue_.HasFinishedInterval(event_time)) {
    MonitorInterval mi = interval_queue_.Pop();
    // hesy uncomment this line  
    std::cerr << "MI Finished with: " << mi.n_packets_sent << ", loss " << mi.GetObsLossRate() << std::endl;
    mi.SetUtility(utility_calculator_->CalculateUtility(interval_analysis_group_, mi));
    rate_control_lock_->lock();
    rate_controller_->MonitorIntervalFinished(mi);  //MI结束了的话就调用MonitorIntervalFinished以调用get_sample 
    rate_control_lock_->unlock();
  }
}

#ifdef QUIC_PORT
bool PccSender::CanSend(QuicByteCount bytes_in_flight) {
  return true;
}
#endif

// 要么保持原样，要么就用下一个MI的目标速度去设定
QuicBandwidth PccSender::PacingRate(UDT_UNUSED QuicByteCount bytes_in_flight) const {
  QuicBandwidth result = interval_queue_.Empty() ? sending_rate_
                                 : interval_queue_.Current().GetTargetSendingRate();
  return result;
}

#ifdef QUIC_PORT
QuicBandwidth PccSender::BandwidthEstimate() const {
  return QuicBandwidth::Zero();
}

QuicByteCount PccSender::GetCongestionWindow() const {
  // Use smoothed_rtt to calculate expected congestion window except when it
  // equals 0, which happens when the connection just starts.
  int64_t rtt_us = rtt_stats_->smoothed_rtt().ToMicroseconds() == 0
                       ? rtt_stats_->initial_rtt_us()
                       : rtt_stats_->smoothed_rtt().ToMicroseconds();
  return static_cast<QuicByteCount>(sending_rate_.ToBytesPerSecond() * rtt_us /
                                    kNumMicrosPerSecond);
}

bool PccSender::InSlowStart() const { return false; }

bool PccSender::InRecovery() const { return false; }

bool PccSender::IsProbingForMoreBandwidth() const { return false; }

QuicByteCount PccSender::GetSlowStartThreshold() const { return 0; }

CongestionControlType PccSender::GetCongestionControlType() const {
  return kPCC;
}

#ifdef QUIC_PORT_LOCAL
std::string PccSender::GetDebugState() const {
#else
string PccSender::GetDebugState() const {
#endif
  if (interval_queue_.empty()) {
    return "pcc??";
  }

  const MonitorInterval& mi = interval_queue_.current();
  std::string msg = QuicStrCat(
      "[st=", mode_, ",", "r=", sending_rate_.ToKBitsPerSecond(), ",",
      "pu=", QuicStringPrintf("%.15g", latest_utility_info_.utility), ",",
      "dir=", direction_, ",", "round=", rounds_, ",",
      "num=", interval_queue_.num_useful_intervals(), "]",
      "[r=", mi.sending_rate.ToKBitsPerSecond(), ",", "use=", mi.is_useful, ",",
      "(", mi.first_packet_sent_time.ToDebuggingValue(), "-", ">",
      mi.last_packet_sent_time.ToDebuggingValue(), ")", "(",
      mi.first_packet_number, "-", ">", mi.last_packet_number, ")", "(",
      mi.bytes_sent, "/", mi.bytes_acked, "/", mi.bytes_lost, ")", "(",
      mi.rtt_on_monitor_start_us, "-", ">", mi.rtt_on_monitor_end_us, ")");
  return msg;
}
#endif

#ifdef QUIC_PORT
} // namespace gfe_quic
#endif

