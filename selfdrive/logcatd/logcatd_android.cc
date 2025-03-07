#include <sys/time.h>
#include <sys/resource.h>

#include <android/log.h>
#include <log/logger.h>
#include <log/logprint.h>

#include "cereal/messaging/messaging.h"
#include "selfdrive/common/util.h"
#include "selfdrive/common/params.h"


// atom
typedef struct LiveMapDataResult {
      float speedLimit;  // Float32;
      float speedLimitDistance;  // Float32;
      float safetySign;    // Float32;
      float roadCurvature;    // Float32;
      float turnInfo;    // Float32;
      float distanceToTurn;    // Float32;
      bool  mapValid;    // bool;
      bool  mapEnable;    // bool;
      long  tv_sec;
      long  tv_nsec;
} LiveMapDataResult;


int main() {
  setpriority(PRIO_PROCESS, 0, -15);
  long  nLastTime = 0, nDelta2 = 0;
  long  nDelta_nsec = 0;
  bool  sBump = false;
  long  tv_nsec;
  float tv_nsec2;

  ExitHandler do_exit;
  PubMaster pm({"liveMapData"});
  LiveMapDataResult res;

  log_time last_log_time = {};
  logger_list *logger_list = android_logger_list_alloc(ANDROID_LOG_RDONLY | ANDROID_LOG_NONBLOCK, 0, 0);

  while (!do_exit) {
    // setup android logging
    if (!logger_list) {
      logger_list = android_logger_list_alloc_time(ANDROID_LOG_RDONLY | ANDROID_LOG_NONBLOCK, last_log_time, 0);
    }
    assert(logger_list);

    struct logger *main_logger = android_logger_open(logger_list, LOG_ID_MAIN);
    assert(main_logger);
   // struct logger *radio_logger = android_logger_open(logger_list, LOG_ID_RADIO);
   // assert(radio_logger);
   // struct logger *system_logger = android_logger_open(logger_list, LOG_ID_SYSTEM);
   // assert(system_logger);
   // struct logger *crash_logger = android_logger_open(logger_list, LOG_ID_CRASH);
   // assert(crash_logger);
   // struct logger *kernel_logger = android_logger_open(logger_list, (log_id_t)5); // LOG_ID_KERNEL
   // assert(kernel_logger);

    while (!do_exit) {
      log_msg log_msg;
      int err = android_logger_list_read(logger_list, &log_msg);
      if (err <= 0) break;

      AndroidLogEntry entry;
      err = android_log_processLogBuffer(&log_msg.entry_v1, &entry);
      if (err < 0) continue;
      last_log_time.tv_sec = entry.tv_sec;
      last_log_time.tv_nsec = entry.tv_nsec;

      tv_nsec2 = entry.tv_nsec / 1000000;
      tv_nsec =  entry.tv_sec * 1000ULL + long(tv_nsec2); // per 1000 = 1s

      MessageBuilder msg;
      auto framed = msg.initEvent().initLiveMapData();

      nDelta2 = entry.tv_sec - nLastTime;
      if( nDelta2 >= 1 )
      {
        nLastTime = entry.tv_sec;
        res.mapEnable = Params().getBool("OpkrMapEnable");
        res.mapValid = Params().getBool("OpkrApksEnable");
        sBump = Params().getBool("OpkrSpeedBump");
      }

   //  opkrspdlimit, opkrspddist, opkrsigntype, opkrcurvangle

      // code based from atom
      nDelta_nsec = tv_nsec - res.tv_nsec;
      //nDelta = entry.tv_sec - res.tv_sec;

      if( strcmp( entry.tag, "opkrspddist" ) == 0 )
      {
        res.tv_sec = entry.tv_sec;
        res.tv_nsec = tv_nsec;
        res.speedLimitDistance = atoi( entry.message );
      }
      else if( strcmp( entry.tag, "opkrspdlimit" ) == 0 )
      {
        res.speedLimit = atoi( entry.message );
      }
      else if( strcmp( entry.tag, "opkrsigntype" ) == 0 )
      {
        res.tv_sec = entry.tv_sec;
        res.tv_nsec = tv_nsec;
        res.safetySign = atoi( entry.message );
        if (res.safetySign == 124) {
          Params().put("OpkrSpeedBump", "1", 1);
          sBump = true;
        }
      }
      else if( (res.speedLimitDistance > 1 && res.speedLimitDistance < 60) && (strcmp( entry.tag, "AudioFlinger" ) == 0) )  //   msm8974_platform
      {
        res.speedLimitDistance = 0;
        res.speedLimit = 0;
        res.safetySign = 0;
        //system("logcat -c &");
      }
      else if( strcmp( entry.tag, "opkrturninfo" ) == 0 )
      {
        res.turnInfo = atoi( entry.message );
      }
      else if( strcmp( entry.tag, "opkrdistancetoturn" ) == 0 )
      {
        res.distanceToTurn = atoi( entry.message );
      }
      else if( nDelta_nsec > 5000 )
      {
        if (res.safetySign == 197 && res.speedLimitDistance < 100) {
          res.speedLimitDistance = 0;
          res.speedLimit = 0;
          res.safetySign = 0;
        }
        else if ( res.safetySign == 124 && (!sBump) )
        {
          res.safetySign = 0;
        }
        else if (res.safetySign != 0 && res.safetySign != 124 && res.speedLimitDistance < 50 && res.speedLimitDistance > 0)
        {
          res.speedLimitDistance = 0;
          res.speedLimit = 0;
          res.safetySign = 0;
        }
        else if( nDelta_nsec > 10000 )
        {
          res.tv_sec = entry.tv_sec;
          res.tv_nsec = tv_nsec;
          res.speedLimitDistance = 0;
          res.speedLimit = 0;
          res.safetySign = 0;
        }
      }

      framed.setSpeedLimit( res.speedLimit );  // Float32;
      framed.setSpeedLimitDistance( res.speedLimitDistance );  // raw_target_speed_map_dist Float32;
      framed.setSafetySign( res.safetySign ); // map_sign Float32;
      // framed.setRoadCurvature( res.roadCurvature ); // road_curvature Float32;
      framed.setTurnInfo( res.turnInfo );  // Float32;
      framed.setDistanceToTurn( res.distanceToTurn );  // Float32;
      framed.setTs( res.tv_sec );
      framed.setMapEnable( res.mapEnable );
      framed.setMapValid( res.mapValid );

    /*
    signtype
    118, 127 어린이보호구역
    111 오른쪽 급커브
    112 왼쪽 급커브
    113 굽은도로
    124 과속방지턱
    198 차선변경금지시작
    199 차선변경금지종료
    129 주정차금지구간
    123 철길건널목
    246 버스전용차로단속
    247 과적단속
    248 교통정보수집
    249 추월금지구간
    250 갓길단속
    251 적재불량단속


    */  
      // printf("tv_nsec = [%ld] tv_nsec2 = [%f]\n", tv_nsec, tv_nsec2);
      // if( opkr )
      // {
      // printf("logcat ID(%d) - PID=%d tag=%d.[%s] \n", log_msg.id(), entry.pid,  entry.tid, entry.tag);
      // printf("entry.message=[%s]\n", entry.message);
      // printf("spd = %f\n", res.speedLimit );
      // printf("spd = %d\n", oTime );
      // }

      pm.send("liveMapData", msg);
    }

    android_logger_list_free(logger_list);
    logger_list = NULL;
    util::sleep_for(500);
  }

  if (logger_list) {
    android_logger_list_free(logger_list);
  }

  return 0;
}
