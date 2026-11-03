/* 检查真实计算模块及 NMEA→CAN 链路，坐标为前、左、上。 */
static void test_gps_motion(void)
{
    gps_motion_t state;
    gps_motion_output_t out;
    gps_motion_imu_t imu={.valid=1,.tick_ms=1000};
    GPS_Motion_Init(&state);
    GPS_Motion_Get(&state,NULL,1000,&out);
    assert(!out.flags&&out.vx_cms==INT16_MIN&&out.vy_cms==INT16_MIN&&out.sample_age_ms==65535);
    for(unsigned direction=0;direction<360;direction+=90) {
        GPS_Motion_Init(&state);
        GPS_Motion_OnHeading(&state,1000,(float)direction,1,1000);
        GPS_Motion_OnRmc(&state,1000,1,3600,(float)direction,1,1000);
        GPS_Motion_Get(&state,&imu,1000,&out);
        assert(out.flags==0x1f&&out.vx_cms==1000&&out.vy_cms==0);
    }
    GPS_Motion_OnRmc(&state,1000,1,3600,0,1,1000);
    GPS_Motion_Get(&state,&imu,1000,&out); /* 车头朝西、向北移动，即向右移动。 */
    assert(abs(out.vx_cms)<=1&&out.vy_cms==-1000);
    GPS_Motion_Init(&state);
    GPS_Motion_OnHeading(&state,1000,90,1,1000);
    GPS_Motion_OnRmc(&state,1000,1,3600,270,1,1000);
    GPS_Motion_Get(&state,&imu,1000,&out);
    assert(out.vx_cms==-1000&&abs(out.vy_cms)<=1);

    /* 从已知质心速度反算 Master 的运动，再检查杆臂修正能恢复质心速度。 */
    for(int turn=-1;turn<=1;turn+=2) {
        float rate=(float)turn*0.5f;
        float mx=10.0f-rate*0.080f, my=rate*(-0.512f);
        float course=90.0f-atan2f(my,mx)/MOTION_RAD_PER_DEG;
        imu.rate_cdeg_s=(int16_t)lroundf(rate/MOTION_RAD_PER_DEG*100.0f);
        GPS_Motion_OnRmc(&state,1000,1,(int32_t)lroundf(hypotf(mx,my)*360.0f),course,1,1000);
        GPS_Motion_Get(&state,&imu,1000,&out);
        assert(out.flags==0x1f&&abs(out.vx_cms-1000)<=1&&abs(out.vy_cms)<=1);
    }

    /* 359°→0°按顺时针跨界；反向跨界与午夜、MCU tick 回绕都不得产生大角速度。 */
    GPS_Motion_Init(&state);
    GPS_Motion_OnHeading(&state,86399900,359,1,UINT32_MAX-99U);
    GPS_Motion_OnHeading(&state,100,1,1,100);
    GPS_Motion_OnRmc(&state,100,1,0,0,1,100);
    GPS_Motion_Get(&state,NULL,100,&out);
    assert(out.flags==0x3f&&out.vx_cms==-1&&out.vy_cms==-9);
    GPS_Motion_OnHeading(&state,100,100,1,150); /* 重复历元不续期。 */
    assert(state.hpr_tick_ms==100);
    GPS_Motion_OnHeading(&state,50,100,1,150);  /* 倒退不续期。 */
    assert(state.hpr_tick_ms==100);
    GPS_Motion_Init(&state);
    GPS_Motion_OnHeading(&state,1000,1,1,1000);
    GPS_Motion_OnHeading(&state,1200,359,1,1200);
    GPS_Motion_OnRmc(&state,1200,1,0,0,1,1200);
    GPS_Motion_Get(&state,NULL,1200,&out);
    assert(out.flags==0x3f&&out.vx_cms==1&&out.vy_cms==9);
    imu=(gps_motion_imu_t){.valid=1,.tick_ms=1200,.rate_cdeg_s=0};
    GPS_Motion_Get(&state,&imu,1300,&out);
    assert(out.flags==0x1f&&out.vx_cms==0&&out.vy_cms==0); /* 100 ms 边界仍优先 IMU。 */
    GPS_Motion_Get(&state,&imu,1301,&out);
    assert(out.flags==0x3f&&out.vy_cms==9); /* 超时才回退。 */
    imu.tick_ms=1301;imu.rate_cdeg_s=-1000;
    GPS_Motion_Get(&state,&imu,1301,&out);
    assert(out.flags==0x1f&&out.vy_cms==-9); /* IMU 恢复，立即使用，符号保持。 */
    GPS_Motion_Get(&state,&imu,1351,&out);
    assert(!(out.flags&GPS_MOTION_VALID)&&out.vx_cms==INT16_MIN); /* RMC 不用旧 500 ms 门限。 */
    GPS_Motion_OnRmc(&state,1501,1,0,0,1,1501);
    GPS_Motion_Get(&state,NULL,1501,&out);
    assert(!(out.flags&GPS_MOTION_YAW_VALID)&&!(out.flags&GPS_MOTION_VALID));
    GPS_Motion_OnHeading(&state,1800,350,1,1800); /* 长间隔重建基准，不能直接差分。 */
    assert(!state.estimate_valid);
    GPS_Motion_OnHeading(&state,2000,200,1,2000); /* 不合理的大跳变拒绝差分。 */
    assert(!state.estimate_valid);

    GPS_Motion_Init(&state);
    imu=(gps_motion_imu_t){.valid=1,.tick_ms=1000,.rate_cdeg_s=1000};
    GPS_Motion_OnHeading(&state,1000,90,1,1000);
    GPS_Motion_OnRmc(&state,1100,1,3600,89,1,1000); /* 左转 10°/s，0.1 s 后车头为 89°。 */
    GPS_Motion_Get(&state,&imu,1000,&out);
    assert(out.flags==0x1f&&out.vx_cms==1001&&out.vy_cms==9);
    GPS_Motion_OnRmc(&state,1400,1,3600,89,1,1000);
    GPS_Motion_Get(&state,&imu,1000,&out);
    assert(!(out.flags&GPS_MOTION_HEADING_VALID)&&!(out.flags&GPS_MOTION_VALID));
    GPS_Motion_OnRmc(&state,1000,1,3600,90,0,1000);
    GPS_Motion_Get(&state,&imu,1000,&out);
    assert(!(out.flags&GPS_MOTION_TRACK_VALID)&&out.vx_cms==INT16_MIN);
    GPS_Motion_OnRmc(&state,1000,1,3600,90,1,1000);
    GPS_Motion_InvalidateRmc(&state);
    GPS_Motion_Get(&state,&imu,1000,&out);
    assert(!(out.flags&GPS_MOTION_SPEED_VALID)&&out.vy_cms==INT16_MIN);
    state.sample_counter=255;
    GPS_Motion_OnRmc(&state,1000,1,0,0,1,UINT32_MAX-20U);
    GPS_Motion_Get(&state,NULL,29,&out);
    assert(out.sample_counter==0&&out.sample_age_ms==50);

    /* 实际接收中断记录时间，任务延迟处理不能重新续期旧 IMU。 */
    gps_test_reset();test_tick=10000;
    assert(CAN1_RX_MSG_ID_BANK6[0]==0x720&&CAN1_RX_MSG_ID_BANK6[3]==0x720);
    memset(test_rx_data,0,8);test_rx_data[0]=0xe8;test_rx_data[1]=3;
    receive(0x720,2,1);test_tick+=101;service_can();
    CAN_GetYawRate(&imu);assert(imu.valid&&imu.tick_ms==10000&&imu.rate_cdeg_s==1000);
    GPS_Motion_OnRmc(&g_gps_motion,43200000,1,3600,90,1,test_tick);
    GPS_Motion_OnHeading(&g_gps_motion,43200000,90,1,test_tick);
    GPS_Motion_Get(&g_gps_motion,&imu,test_tick,&out);
    assert(!(out.flags&GPS_MOTION_YAW_VALID));
    unsigned invalid=g_can_rx_invalid_count;
    receive(0x720,1,1);service_can();assert(g_can_rx_invalid_count==invalid+1);
    receive(0x720,8,1);service_can(); /* 允许较长帧，只读已确认的前两字节。 */
    nmea("GPHPR,120000.00,90.0,0,0,4");
    rmc(43200000,'A',"19.44");
    gps_can_speed_tick(test_tick);
    assert(test_tx_header.StdId==0x069&&test_tx_header.DLC==8);
    assert((int16_t)can_u16_le(test_tx_id_data[0x069],0)==1001);
    assert((int16_t)can_u16_le(test_tx_id_data[0x069],2)==9);
    assert(test_tx_id_data[0x069][4]==0x1f&&test_tx_id_data[0x069][5]==2);
    test_tick+=50;
    nmea("GNRMC,120000.05,A,3200.000,N,11800.000,E,19.44,,181026,,,A,V");
    gps_can_speed_tick(test_tick);
    assert(!(test_tx_id_data[0x069][4]&GPS_MOTION_TRACK_VALID));
    assert((int16_t)can_u16_le(test_tx_id_data[0x069],0)==INT16_MIN);
    test_tick+=150;
    nmea("GPHPR,120000.20,88.0,0,0,4");
    nmea("GNRMC,120000.20,A,3200.000,N,11800.000,E,19.44,88.0,181026,,,A,V");
    gps_can_speed_tick(test_tick);
    assert(test_tx_id_data[0x069][4]==0x3f); /* 真实 NMEA→差分回退→CAN。 */
    assert((int16_t)can_u16_le(test_tx_id_data[0x069],2)==9);
    test_tick+=50;
    test_rx_data[0]=0x18;test_rx_data[1]=0xfc;receive(0x720,2,1);service_can();
    rmc(43200250,'A',"0.00");gps_can_speed_tick(test_tick);
    assert(test_tx_id_data[0x069][4]==0x1f);
    assert((int16_t)can_u16_le(test_tx_id_data[0x069],2)==-9);
    test_tick+=50;rmc(43200300,'V',"0.00");gps_can_speed_tick(test_tick);
    assert(!(test_tx_id_data[0x069][4]&GPS_MOTION_VALID));
    assert((int16_t)can_u16_le(test_tx_id_data[0x069],2)==INT16_MIN);
    float angle;
    assert(!gps_parse_angle("$GPRMC,nan,",1,&angle)&&!gps_parse_angle("$GPRMC,361,",1,&angle));
    assert(!gps_parse_angle("$GPRMC,.,",1,&angle)&&!gps_parse_angle("$GPRMC,,",1,&angle));
    puts("CG velocity: rigid lever arm, body axes, GNSS epoch alignment, IMU priority/timeout/recovery, heading fallback/wrap/gaps, invalid data and NMEA-to-CAN passed");
}
