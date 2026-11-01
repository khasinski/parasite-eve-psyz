/* Original project per-raster contract. Requested settings configure future
 * draws; already drawn VRAM must retain the projection that created it. */
typedef struct {
    bool valid,wide,pal;
    int x,y,w,h,horiz,vert;
    PsyzAspectMode aspect;
    unsigned long long serial;
    unsigned char picture[sizeof(ws_picture)];
    unsigned char room[sizeof(resolution_room)];
    float mid_qx[2];
    float mid_qy[2];
    float mid_shift[2];
    float mid_ex[2];
    float mid_ey[2];
    float mid_left[2];
    float mid_right[2];
} CommittedPresentation;
typedef struct {
    bool active,wide,pal;
    int horiz,vert;
    PsyzAspectMode aspect;
    unsigned char picture[sizeof(ws_picture)];
    unsigned char room[sizeof(resolution_room)];
    float mid_qx[2];
    float mid_qy[2];
    float mid_shift[2];
    float mid_ex[2];
    float mid_ey[2];
    float mid_left[2];
    float mid_right[2];
} PresentationScope;
static CommittedPresentation committed_presentation[2];
static unsigned long long committed_draw_serial;
static unsigned committed_log_count;
static int committed_last_pending=-1,committed_last_wide=-1,committed_last_aspect=-1;
static int committed_last_requested_wide=-1,committed_last_requested_aspect=-1;

/* Called after actual nonempty display-buffer draw commands are recorded,
 * never from the host modal/UI presentation loop. */
static void CommitDrawPresentation(void){
    if(!DisplayBufferDraw())return;
    int y=(draw_area_start.y/display_size.y)*display_size.y;
    int slot=-1;for(int i=0;i<2;i++)if(committed_presentation[i].valid&&committed_presentation[i].y==y)slot=i;
    if(slot<0)for(int i=0;i<2;i++)if(!committed_presentation[i].valid){slot=i;break;}
    if(slot<0)slot=committed_presentation[0].serial<committed_presentation[1].serial?0:1;
    CommittedPresentation *f=&committed_presentation[slot];
    f->valid=true;f->wide=widescreen;f->aspect=aspect_mode;f->pal=is_pal;
    f->x=display_area.x;f->y=y;f->w=display_size.x;f->h=display_size.y;
    f->horiz=set_disp_horiz;f->vert=set_disp_vert;f->serial=++committed_draw_serial;
    memcpy(f->picture,ws_picture,sizeof(ws_picture));
    memcpy(f->room,resolution_room,sizeof(resolution_room));
    memcpy(f->mid_qx,ws_mid_qx,sizeof(ws_mid_qx));
    memcpy(f->mid_qy,ws_mid_qy,sizeof(ws_mid_qy));
    memcpy(f->mid_shift,ws_mid_shift,sizeof(ws_mid_shift));
    memcpy(f->mid_ex,ws_mid_ex,sizeof(ws_mid_ex));
    memcpy(f->mid_ey,ws_mid_ey,sizeof(ws_mid_ey));
    memcpy(f->mid_left,ws_mid_left,sizeof(ws_mid_left));
    memcpy(f->mid_right,ws_mid_right,sizeof(ws_mid_right));
}
static PresentationScope BeginCommittedPresentation(bool game_image){
    PresentationScope scope={0};if(!game_image)return scope;
    CommittedPresentation *f=NULL;
    for(int i=0;i<2;i++){CommittedPresentation *candidate=&committed_presentation[i];
        if(candidate->valid&&candidate->x==display_area.x&&candidate->y==(interp_present_mid?interp_next_y:display_area.y)&&
           candidate->w==display_size.x&&candidate->h==display_size.y)f=candidate;}
    if(!f)return scope; /* No matching raster yet: retain baseline startup path. */
    int pending=f->wide!=widescreen||f->aspect!=aspect_mode;
    if(committed_log_count<64&&(pending!=committed_last_pending||f->wide!=committed_last_wide||
        f->aspect!=committed_last_aspect||widescreen!=committed_last_requested_wide||aspect_mode!=committed_last_requested_aspect)){
        INFOF("DISPLAY committed-raster requested_wide=%d requested_aspect=%d raster_wide=%d raster_aspect=%d pending_new_game_draw=%d buffer=%d,%d/%dx%d draw_serial=%llu mode=game-image not_stretch=1 rendered=unobserved",widescreen,aspect_mode,f->wide,f->aspect,pending,f->x,f->y,f->w,f->h,f->serial);
        committed_log_count++;committed_last_pending=pending;committed_last_wide=f->wide;committed_last_aspect=f->aspect;
        committed_last_requested_wide=widescreen;committed_last_requested_aspect=aspect_mode;
    }
    scope.active=true;scope.wide=widescreen;scope.aspect=aspect_mode;scope.pal=is_pal;
    scope.horiz=set_disp_horiz;scope.vert=set_disp_vert;
    memcpy(scope.mid_qx,ws_mid_qx,sizeof(ws_mid_qx));
    memcpy(scope.mid_qy,ws_mid_qy,sizeof(ws_mid_qy));
    memcpy(scope.mid_shift,ws_mid_shift,sizeof(ws_mid_shift));
    memcpy(scope.mid_ex,ws_mid_ex,sizeof(ws_mid_ex));
    memcpy(scope.mid_ey,ws_mid_ey,sizeof(ws_mid_ey));
    memcpy(scope.mid_left,ws_mid_left,sizeof(ws_mid_left));
    memcpy(scope.mid_right,ws_mid_right,sizeof(ws_mid_right));
    memcpy(scope.picture,ws_picture,sizeof(ws_picture));memcpy(scope.room,resolution_room,sizeof(resolution_room));
    widescreen=f->wide;aspect_mode=f->aspect;is_pal=f->pal;set_disp_horiz=f->horiz;set_disp_vert=f->vert;
    memcpy(ws_mid_qx,f->mid_qx,sizeof(ws_mid_qx));
    memcpy(ws_mid_qy,f->mid_qy,sizeof(ws_mid_qy));
    memcpy(ws_mid_shift,f->mid_shift,sizeof(ws_mid_shift));
    memcpy(ws_mid_ex,f->mid_ex,sizeof(ws_mid_ex));
    memcpy(ws_mid_ey,f->mid_ey,sizeof(ws_mid_ey));
    memcpy(ws_mid_left,f->mid_left,sizeof(ws_mid_left));
    memcpy(ws_mid_right,f->mid_right,sizeof(ws_mid_right));
    memcpy(ws_picture,f->picture,sizeof(ws_picture));memcpy(resolution_room,f->room,sizeof(resolution_room));
    return scope;
}
static void EndCommittedPresentation(const PresentationScope *scope){
    if(!scope->active)return;
    widescreen=scope->wide;aspect_mode=scope->aspect;is_pal=scope->pal;
    set_disp_horiz=scope->horiz;set_disp_vert=scope->vert;
    memcpy(ws_mid_qx,scope->mid_qx,sizeof(ws_mid_qx));
    memcpy(ws_mid_qy,scope->mid_qy,sizeof(ws_mid_qy));
    memcpy(ws_mid_shift,scope->mid_shift,sizeof(ws_mid_shift));
    memcpy(ws_mid_ex,scope->mid_ex,sizeof(ws_mid_ex));
    memcpy(ws_mid_ey,scope->mid_ey,sizeof(ws_mid_ey));
    memcpy(ws_mid_left,scope->mid_left,sizeof(ws_mid_left));
    memcpy(ws_mid_right,scope->mid_right,sizeof(ws_mid_right));
    memcpy(ws_picture,scope->picture,sizeof(ws_picture));memcpy(resolution_room,scope->room,sizeof(resolution_room));
}
