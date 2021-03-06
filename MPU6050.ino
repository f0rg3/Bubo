#ifdef Mpu6050Action
  #include "I2Cdev.h"
  
  #include "MPU6050_6Axis_MotionApps20.h"
  
  #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
      #include "Wire.h"
  #endif
  
  // class default I2C address is 0x68
  // specific I2C addresses may be passed as a parameter here
  // AD0 low = 0x68 (default for SparkFun breakout and InvenSense evaluation board)
  // AD0 high = 0x69
  MPU6050 mpu;
  //MPU6050 mpu(0x69); // <-- use for AD0 high
  
  /* =========================================================================
     NOTE: In addition to connection 3.3v, GND, SDA, and SCL, this sketch
     depends on the MPU-6050's INT pin being connected to the Arduino's
     external interrupt #0 pin. On the Arduino Uno and Mega 2560, this is
     digital I/O pin 2.
   * ========================================================================= */
  
  // uncomment "OUTPUT_READABLE_YAWPITCHROLL" if you want to see the yaw/
  // pitch/roll angles (in degrees) calculated from the quaternions coming
  // from the FIFO. Note this also requires gravity vector calculations.
  // Also note that yaw/pitch/roll angles suffer from gimbal lock (for
  // more info, see: http://en.wikipedia.org/wiki/Gimbal_lock)
  #define OUTPUT_READABLE_YAWPITCHROLL
  
 
  bool blinkState = false;
  
  // MPU control/status vars
  bool dmpReady = false;  // set true if DMP init was successful
  uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
  uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
  uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
  uint16_t fifoCount;     // count of all bytes currently in FIFO
  uint8_t fifoBuffer[64]; // FIFO storage buffer
  
  // orientation/motion vars
  Quaternion q;           // [w, x, y, z]         quaternion container
  VectorInt16 aa;         // [x, y, z]            accel sensor measurements
  VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
  VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
  VectorFloat gravity;    // [x, y, z]            gravity vector
  float euler[3];         // [psi, theta, phi]    Euler angle container
  float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector
  
  // packet structure for InvenSense teapot demo
  uint8_t teapotPacket[14] = { '$', 0x02, 0,0, 0,0, 0,0, 0,0, 0x00, 0x00, '\r', '\n' };
  
  int lastyaw;
  
  // ================================================================
  // ===               INTERRUPT DETECTION ROUTINE                ===
  // ================================================================
  
  volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
  void dmpDataReady() {
      mpuInterrupt = true;
  }
  
  void init_mpu6050() {
      #ifdef Debug
        Serial.print("Init mpu6050");
      #endif
      // join I2C bus (I2Cdev library doesn't do this automatically)
      #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
          Wire.begin();
          TWBR = 24; // 400kHz I2C clock (200kHz if CPU is 8MHz)
      #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
          Fastwire::setup(400, true);
      #endif
  
      // initialize device
      #ifdef Debug
        Serial.println(F("Initializing I2C devices..."));
      #endif
      mpu.initialize();
  
      // verify connection
      #ifdef Debug
        Serial.println(F("Testing device connections..."));
        Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));
        // wait for ready
        //Serial.println(F("\nSend any character to begin DMP programming and demo: "));
        //while (Serial.available() && Serial.read()); // empty buffer
        //while (!Serial.available());                 // wait for data
        //while (Serial.available() && Serial.read()); // empty buffer again
  
        // load and configure the DMP
        Serial.println(F("Initializing DMP..."));
      #endif
      devStatus = mpu.dmpInitialize();
  
      // supply your own gyro offsets here, scaled for min sensitivity
      mpu.setXGyroOffset(220);
      mpu.setYGyroOffset(76);
      mpu.setZGyroOffset(-85);
      mpu.setZAccelOffset(1788); // 1688 factory default for my test chip
  
      // make sure it worked (returns 0 if so)
      if (devStatus == 0) {
          // turn on the DMP, now that it's ready
          #ifdef Debug
            Serial.println(F("Enabling DMP..."));
          #endif
          mpu.setDMPEnabled(true);
  
          // enable Arduino interrupt detection
          #ifdef Debug
            Serial.println(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
          #endif
          attachInterrupt(0, dmpDataReady, RISING);
          mpuIntStatus = mpu.getIntStatus();
  
          // set our DMP Ready flag so the main loop() function knows it's okay to use it
          #ifdef Debug
            Serial.println(F("DMP ready! Waiting for first interrupt..."));
          #endif
          dmpReady = true;
  
          // get expected DMP packet size for later comparison
          packetSize = mpu.dmpGetFIFOPacketSize();
      } else {
          // ERROR!
          // 1 = initial memory load failed
          // 2 = DMP configuration updates failed
          // (if it's going to break, usually the code will be 1)
          #ifdef Debug
            Serial.print(F("DMP Initialization failed (code "));
            Serial.print(devStatus);
            Serial.println(F(")"));
          #endif
      }
  
  
      
      Queue(50,Mpu6050Action,1,0);
  }

  void do_Mpu6050(int action, int actiondata){
    #ifdef Debug
      Serial.print("Mpu6050Action: ");
      Serial.println(action);
    #endif
    switch (action) {
      case 0: // off
        break;
      case 1: // on
        read_neck();
      case 2: // reschedule
        Queue(50,Mpu6050Action,1,0);
        break;
    }
  }

  
  int read_neck(){
    int yaw;
    int rotation;
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();
    fifoCount = mpu.getFIFOCount();
    if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
      mpu.resetFIFO();
      Serial.println(F("FIFO overflow!"));
      } else if (mpuIntStatus & 0x02) {
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        fifoCount -= packetSize;
        mpu.dmpGetQuaternion(&q, fifoBuffer);
        mpu.dmpGetGravity(&gravity, &q);
        mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
        yaw=ypr[0] * 180/M_PI;
        if (lastyaw>160 && yaw<-160){
          rotation = (yaw+360) - lastyaw;
        } else if (lastyaw<-160 && yaw>160){
          rotation = (yaw-360) - lastyaw;
        } else {
          rotation = yaw - lastyaw;
        }
        lastyaw=yaw;
        #ifdef Debug
          Serial.println(yaw);
        #endif
        return(rotation);
  //      Serial.print("ypr\t");
  //            Serial.print(ypr[0] * 180/M_PI);
  //            Serial.print("\t");
  //            Serial.print(ypr[1] * 180/M_PI);
  //            Serial.print("\t");
  //            Serial.println(ypr[2] * 180/M_PI);
  //        #endif
  //       a ly - b y
  //    150 ->  170 = +20  = b - a
  //    170 -> -170 = +20  = (b+360) - a
  //   -170 ->  170 = -20  = (b-360) - a
      }
  }
  
  void read_mpu6050(){
      // reset interrupt flag and get INT_STATUS byte
      mpuInterrupt = false;
      mpuIntStatus = mpu.getIntStatus();
  
      // get current FIFO count
      fifoCount = mpu.getFIFOCount();
  
      // check for overflow (this should never happen unless our code is too inefficient)
      if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
          // reset so we can continue cleanly
          mpu.resetFIFO();
          Serial.println(F("FIFO overflow!"));
  
      // otherwise, check for DMP data ready interrupt (this should happen frequently)
      } else if (mpuIntStatus & 0x02) {
          // wait for correct available data length, should be a VERY short wait
          while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
  
          // read a packet from FIFO
          mpu.getFIFOBytes(fifoBuffer, packetSize);
          
          // track FIFO count here in case there is > 1 packet available
          // (this lets us immediately read more without waiting for an interrupt)
          fifoCount -= packetSize;
  
          #ifdef OUTPUT_READABLE_QUATERNION
              // display quaternion values in easy matrix form: w x y z
              mpu.dmpGetQuaternion(&q, fifoBuffer);
              Serial.print("quat\t");
              Serial.print(q.w);
              Serial.print("\t");
              Serial.print(q.x);
              Serial.print("\t");
              Serial.print(q.y);
              Serial.print("\t");
              Serial.println(q.z);
          #endif
  
          #ifdef OUTPUT_READABLE_EULER
              // display Euler angles in degrees
              mpu.dmpGetQuaternion(&q, fifoBuffer);
              mpu.dmpGetEuler(euler, &q);
              Serial.print("euler\t");
              Serial.print(euler[0] * 180/M_PI);
              Serial.print("\t");
              Serial.print(euler[1] * 180/M_PI);
              Serial.print("\t");
              Serial.println(euler[2] * 180/M_PI);
          #endif
  
          #ifdef OUTPUT_READABLE_YAWPITCHROLL
              // display Euler angles in degrees
              mpu.dmpGetQuaternion(&q, fifoBuffer);
              mpu.dmpGetGravity(&gravity, &q);
              mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
              Serial.print("ypr\t");
              Serial.print(ypr[0] * 180/M_PI);
              Serial.print("\t");
              Serial.print(ypr[1] * 180/M_PI);
              Serial.print("\t");
              Serial.println(ypr[2] * 180/M_PI);
          #endif
  
          #ifdef OUTPUT_READABLE_REALACCEL
              // display real acceleration, adjusted to remove gravity
              mpu.dmpGetQuaternion(&q, fifoBuffer);
              mpu.dmpGetAccel(&aa, fifoBuffer);
              mpu.dmpGetGravity(&gravity, &q);
              mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
              Serial.print("areal\t");
              Serial.print(aaReal.x);
              Serial.print("\t");
              Serial.print(aaReal.y);
              Serial.print("\t");
              Serial.println(aaReal.z);
          #endif
  
          #ifdef OUTPUT_READABLE_WORLDACCEL
              // display initial world-frame acceleration, adjusted to remove gravity
              // and rotated based on known orientation from quaternion
              mpu.dmpGetQuaternion(&q, fifoBuffer);
              mpu.dmpGetAccel(&aa, fifoBuffer);
              mpu.dmpGetGravity(&gravity, &q);
              mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
              mpu.dmpGetLinearAccelInWorld(&aaWorld, &aaReal, &q);
              Serial.print("aworld\t");
              Serial.print(aaWorld.x);
              Serial.print("\t");
              Serial.print(aaWorld.y);
              Serial.print("\t");
              Serial.println(aaWorld.z);
          #endif
      
          #ifdef OUTPUT_TEAPOT
              // display quaternion values in InvenSense Teapot demo format:
              teapotPacket[2] = fifoBuffer[0];
              teapotPacket[3] = fifoBuffer[1];
              teapotPacket[4] = fifoBuffer[4];
              teapotPacket[5] = fifoBuffer[5];
              teapotPacket[6] = fifoBuffer[8];
              teapotPacket[7] = fifoBuffer[9];
              teapotPacket[8] = fifoBuffer[12];
              teapotPacket[9] = fifoBuffer[13];
              Serial.write(teapotPacket, 14);
              teapotPacket[11]++; // packetCount, loops at 0xFF on purpose
          #endif
  
           
      }
  
  }
#endif
