/* SPDX-License-Identifier: MIT -- native movie presentation ownership.
 * A stop request is not proof that the display environment changed. */
#ifndef PE_MOVIE_LIFECYCLE_H
#define PE_MOVIE_LIFECYCLE_H
static int native_movie_active;
/* One resource retry budget per actual successful original movie open.
 * Repeated Active1 on the same running source never advances this serial. */
static unsigned native_movie_source_serial;
/* 0 running/none,1 restoring saved game display,2 awaiting PutDispEnv,
 * 3 awaiting a post-environment original OT,4 awaiting copied-queue consumption. */
static unsigned native_movie_retire_phase;
static int native_movie_flush_failed;
static unsigned native_movie_lifecycle_logs;
static void MovieLifecycleTrace(const char*stage){if(native_movie_lifecycle_logs>=64)return;++native_movie_lifecycle_logs;INFOF("Movie ownership stage=%s active=%d retire_phase=%u flush_failed=%d (source/queue witness; pixels unobserved)",stage,native_movie_active,native_movie_retire_phase,native_movie_flush_failed);}
void Psyz_VideoSetMovieActive(int active){
 if(active){if((!native_movie_active||native_movie_retire_phase)&&native_movie_source_serial!=~0u)++native_movie_source_serial;native_movie_active=1;native_movie_retire_phase=0;native_movie_flush_failed=0;MovieLifecycleTrace("movie-start");}
 else if(native_movie_active&&!native_movie_retire_phase){native_movie_retire_phase=1;native_movie_flush_failed=0;MovieLifecycleTrace("retire-request");}
}
void Psyz_VideoMovieRestoreReady(void){
 if(native_movie_active&&native_movie_retire_phase==1){native_movie_retire_phase=2;MovieLifecycleTrace("saved-display-restored");}
}
void Psyz_VideoMovieDisplayEnvSubmitted(void){
 if(native_movie_active&&native_movie_retire_phase>=2){native_movie_retire_phase=3;native_movie_flush_failed=0;MovieLifecycleTrace("game-display-environment");}
}
void Psyz_VideoMovieFrameQueued(int accepted){
 if(native_movie_active&&native_movie_retire_phase>=3){
  if(accepted){native_movie_retire_phase=4;MovieLifecycleTrace("post-environment-frame-queued");}
  else{native_movie_retire_phase=3;native_movie_flush_failed=1;MovieLifecycleTrace("frame-admission-failed");}
 }
}
static void MovieLifecycleFlushFailed(void){
 if(native_movie_active&&native_movie_retire_phase>=2){native_movie_flush_failed=1;MovieLifecycleTrace("frame-flush-failed");}
}
void Psyz_VideoMovieQueueConsumed(int success){
 if(!native_movie_active||native_movie_retire_phase!=4)return;
 if(!success||gpu_failed||native_movie_flush_failed){native_movie_retire_phase=3;MovieLifecycleTrace("frame-consumption-failed");return;}
 native_movie_active=0;native_movie_retire_phase=0;native_movie_flush_failed=0;MovieLifecycleTrace("game-frame-prepared");
}
#endif
