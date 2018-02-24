#include "OpenFAST.H"

//Constructor 
fast::fastInputs::fastInputs():
nTurbinesGlob(0),
dryRun(false),
debug(false),
tStart(-1.0),
nEveryCheckPoint(-1),
tMax(0.0),
dtFAST(0.0),
scStatus(false),
scLibFile(""),
numScInputs(0),
numScOutputs(0)
{
  //Nothing to do here
}


//Constructor
fast::OpenFAST::OpenFAST():
nTurbinesGlob(0),
nTurbinesProc(0),
scStatus(false),
simStart(fast::init),
timeZero(false),
firstPass_(true)
{
}

inline bool fast::OpenFAST::checkFileExists(const std::string& name) {
  struct stat buffer;   
  return (stat (name.c_str(), &buffer) == 0); 
}

void fast::OpenFAST::init() {

  allocateMemory();

  if (!dryRun) {
    switch (simStart) {

    case fast::trueRestart:

     for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
         /* note that this will set nt_global inside the FAST library */
         FAST_OpFM_Restart(&iTurb, turbineData[iTurb].FASTRestartFileName.data(), &AbortErrLev, &dtFAST, &turbineData[iTurb].inflowType, &turbineData[iTurb].numBlades, &turbineData[iTurb].numVelPtsBlade, &turbineData[iTurb].numVelPtsTwr, &ntStart, &i_f_FAST[iTurb], &o_t_FAST[iTurb], &sc_i_f_FAST[iTurb], &sc_o_t_FAST[iTurb], &ErrStat, ErrMsg);
       checkError(ErrStat, ErrMsg);
       nt_global = ntStart;

       allocateMemory2(iTurb);

     }

     if (nTurbinesProc > 0) velNodeDataFile = openVelocityDataFile(false);

     if(scStatus) {
	 sc->readRestartFile(nt_global);
     }
     
     break ;
   
    case fast::init:
     
      // this calls the Init() routines of each module

     for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
         FAST_OpFM_Init(&iTurb, &tMax, turbineData[iTurb].FASTInputFileName.data(), &turbineData[iTurb].TurbID, &numScOutputs, &numScInputs, &turbineData[iTurb].numForcePtsBlade, &turbineData[iTurb].numForcePtsTwr, turbineData[iTurb].TurbineBasePos.data(), &AbortErrLev, &dtFAST, &turbineData[iTurb].inflowType, &turbineData[iTurb].numBlades, &turbineData[iTurb].numVelPtsBlade, &turbineData[iTurb].numVelPtsTwr, &i_f_FAST[iTurb], &o_t_FAST[iTurb], &sc_i_f_FAST[iTurb], &sc_o_t_FAST[iTurb], &ErrStat, ErrMsg);
       checkError(ErrStat, ErrMsg);

       timeZero = true;
      
       allocateMemory2(iTurb);
      
     }

     if (nTurbinesProc > 0) velNodeDataFile = openVelocityDataFile(true);

     break ;

    case fast::restartDriverInitFAST:

     for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
         FAST_OpFM_Init(&iTurb, &tMax, turbineData[iTurb].FASTInputFileName.data(), &turbineData[iTurb].TurbID, &numScOutputs, &numScInputs, &turbineData[iTurb].numForcePtsBlade, &turbineData[iTurb].numForcePtsTwr, turbineData[iTurb].TurbineBasePos.data(), &AbortErrLev, &dtFAST, &turbineData[iTurb].inflowType, &turbineData[iTurb].numBlades, &turbineData[iTurb].numVelPtsBlade, &turbineData[iTurb].numVelPtsTwr, &i_f_FAST[iTurb], &o_t_FAST[iTurb], &sc_i_f_FAST[iTurb], &sc_o_t_FAST[iTurb], &ErrStat, ErrMsg);
       checkError(ErrStat, ErrMsg);
       
       timeZero = true;

       allocateMemory2(iTurb);

     }

     int nTimesteps;
     
     if (nTurbinesProc > 0) {
       readVelocityData(ntStart);
     }
     for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
       applyVelocityData(0, iTurb, o_t_FAST[iTurb], velNodeData[iTurb]);
     }
     solution0() ;

     for (int iPrestart=0 ; iPrestart < ntStart; iPrestart++) {
       for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
	 applyVelocityData(iPrestart, iTurb, o_t_FAST[iTurb], velNodeData[iTurb]);
       }
       stepNoWrite();
     }

     if (nTurbinesProc > 0) velNodeDataFile = openVelocityDataFile(false);

     break;
      
    case fast::simStartType_END:

      break;
     
    }

  }
}

void fast::OpenFAST::solution0() {

  if (!dryRun) {
      
    // set wind speeds at initial locations
    // for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
    //     setOutputsToFAST(i_f_FAST[iTurb], o_t_FAST[iTurb]);
    // }
     
     if(scStatus) {

       sc->init(nTurbinesGlob, numScInputs, numScOutputs);

       sc->calcOutputs(scOutputsGlob);
       fillScOutputsLoc();
     }

     for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {

       FAST_OpFM_Solution0(&iTurb, &ErrStat, ErrMsg);
       checkError(ErrStat, ErrMsg);

     }

     get_data_from_openfast(fast::n);

     timeZero = false;

     if (scStatus) {
       fillScInputsGlob(); // Update inputs to super controller
     }
  }

}

void fast::OpenFAST:: predict_states() {

    if (firstPass_) {
        for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
            int nvelpts = get_numVelPtsLoc(iTurb);
            int nfpts = get_numForcePtsLoc(iTurb);
            for (int i=0; i<nvelpts; i++) {
                for (int j=0 ; j < 3; j++) {
                    velForceNodeData[iTurb][fast::np1].x_vel[i*3+j] = velForceNodeData[iTurb][fast::n].x_vel[i*3+j] + 0.5*(3.0*velForceNodeData[iTurb][fast::n].xdot_vel[i*3+j] - velForceNodeData[iTurb][fast::nm1].xdot_vel[i*3+j]);
                    velForceNodeData[iTurb][fast::np1].xdot_vel[i*3+j] = 2.0*velForceNodeData[iTurb][fast::n].xdot_vel[i*3+j] - velForceNodeData[iTurb][fast::nm1].xdot_vel[i*3+j];
                    velForceNodeData[iTurb][fast::np1].vel_vel[i*3+j] = 2.0*velForceNodeData[iTurb][fast::n].vel_vel[i*3+j] - velForceNodeData[iTurb][fast::nm1].vel_vel[i*3+j];
                }
            }
            velForceNodeData[iTurb][fast::np1].x_vel_resid = 0.0;
            velForceNodeData[iTurb][fast::np1].xdot_vel_resid = 0.0;
            velForceNodeData[iTurb][fast::np1].vel_vel_resid = 0.0;        
            for (int i=0; i<nfpts; i++) {
                for (int j=0 ; j < 3; j++) {
                    velForceNodeData[iTurb][fast::np1].x_force[i*3+j] = velForceNodeData[iTurb][fast::n].x_force[i*3+j] + 0.5*(3.0*velForceNodeData[iTurb][fast::n].xdot_force[i*3+j] - velForceNodeData[iTurb][fast::nm1].xdot_force[i*3+j]);
                    velForceNodeData[iTurb][fast::np1].xdot_force[i*3+j] = 2.0*velForceNodeData[iTurb][fast::n].xdot_force[i*3+j] - velForceNodeData[iTurb][fast::nm1].xdot_force[i*3+j];
                    velForceNodeData[iTurb][fast::np1].vel_force[i*3+j] = 2.0*velForceNodeData[iTurb][fast::n].vel_force[i*3+j] - velForceNodeData[iTurb][fast::nm1].vel_force[i*3+j];
                    velForceNodeData[iTurb][fast::np1].force[i*3+j] = 2.0*velForceNodeData[iTurb][fast::n].force[i*3+j] - velForceNodeData[iTurb][fast::nm1].force[i*3+j];
                }
                for (int j=0;j<9;j++)
                    velForceNodeData[iTurb][fast::np1].orient_force[i*9+j] = 2.0*velForceNodeData[iTurb][fast::n].orient_force[i*9+j] - velForceNodeData[iTurb][fast::nm1].orient_force[i*9+j];
            }
            velForceNodeData[iTurb][fast::np1].x_force_resid = 0.0;
            velForceNodeData[iTurb][fast::np1].xdot_force_resid = 0.0;
            velForceNodeData[iTurb][fast::np1].orient_force_resid = 0.0;
            velForceNodeData[iTurb][fast::np1].vel_force_resid = 0.0;
            velForceNodeData[iTurb][fast::np1].force_resid = 0.0;
        }
    }
}

void fast::OpenFAST::prework() {

    if(scStatus) {
        sc->calcOutputs(scOutputsGlob);
        fillScOutputsLoc();
    }

    for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
        FAST_OpFM_Prework(&iTurb, &ErrStat, ErrMsg);
        checkError(ErrStat, ErrMsg);
    }

}

void fast::OpenFAST::update_states() {

   if (firstPass_)
       prework();

   send_data_to_openfast(fast::np1);
   
   for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
     FAST_OpFM_UpdateStates(&iTurb, &ErrStat, ErrMsg);
     checkError(ErrStat, ErrMsg);
   }
   
   get_data_from_openfast(fast::np1);

}

void fast::OpenFAST::advance_to_next_time_step() {
    
   for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {

     writeVelocityData(velNodeDataFile, iTurb, nt_global, i_f_FAST[iTurb], o_t_FAST[iTurb]);

     if ( isDebug() && (turbineData[iTurb].inflowType == 2) ) {

       std::ofstream fastcpp_velocity_file;
       fastcpp_velocity_file.open("fastcpp_velocity.csv") ;
       fastcpp_velocity_file << "# x, y, z, Vx, Vy, Vz" << std::endl ;
       for (int iNode=0; iNode < get_numVelPtsLoc(iTurb); iNode++) {
	 fastcpp_velocity_file << i_f_FAST[iTurb].pxVel[iNode] << ", " << i_f_FAST[iTurb].pyVel[iNode] << ", " << i_f_FAST[iTurb].pzVel[iNode] << ", " << o_t_FAST[iTurb].u[iNode] << ", " << o_t_FAST[iTurb].v[iNode] << ", " << o_t_FAST[iTurb].w[iNode] << " " << std::endl ;           
       }
       fastcpp_velocity_file.close() ;
       
     }
     
     FAST_OpFM_AdvanceToNextTimeStep(&iTurb, &ErrStat, ErrMsg);
     checkError(ErrStat, ErrMsg);

     if ( isDebug() && (turbineData[iTurb].inflowType == 2) ) {
       std::ofstream actuatorForcesFile;
       actuatorForcesFile.open("actuator_forces.csv") ;
       actuatorForcesFile << "# x, y, z, fx, fy, fz" << std::endl ;
       for (int iNode=0; iNode < get_numForcePtsLoc(iTurb); iNode++) {
	 actuatorForcesFile << i_f_FAST[iTurb].pxForce[iNode] << ", " << i_f_FAST[iTurb].pyForce[iNode] << ", " << i_f_FAST[iTurb].pzForce[iNode] << ", " << i_f_FAST[iTurb].fx[iNode] << ", " << i_f_FAST[iTurb].fy[iNode] << ", " << i_f_FAST[iTurb].fz[iNode] << " " << std::endl ;           
       }
       actuatorForcesFile.close() ;
     }

     int nvelpts = get_numVelPtsLoc(iTurb);
     int nfpts = get_numForcePtsLoc(iTurb);
     for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
         int nvelpts = get_numVelPtsLoc(iTurb);
         int nfpts = get_numForcePtsLoc(iTurb);
         for (int i=0; i<nvelpts; i++) {
             for (int j=0 ; j < 3; j++) {
                 velForceNodeData[iTurb][fast::nm1].x_vel[i*3+j] = velForceNodeData[iTurb][fast::n].x_vel[i*3+j];
                 velForceNodeData[iTurb][fast::n].x_vel[i*3+j] = velForceNodeData[iTurb][fast::np1].x_vel[i*3+j];
                 velForceNodeData[iTurb][fast::nm1].xdot_vel[i*3+j] = velForceNodeData[iTurb][fast::n].xdot_vel[i*3+j];
                 velForceNodeData[iTurb][fast::n].xdot_vel[i*3+j] = velForceNodeData[iTurb][fast::np1].xdot_vel[i*3+j];
                 velForceNodeData[iTurb][fast::nm1].vel_vel[i*3+j] = velForceNodeData[iTurb][fast::n].vel_vel[i*3+j];
                 velForceNodeData[iTurb][fast::n].vel_vel[i*3+j] = velForceNodeData[iTurb][fast::np1].vel_vel[i*3+j];
             }
         }
         for (int i=0; i<nfpts; i++) {
             for (int j=0 ; j < 3; j++) {
                 velForceNodeData[iTurb][fast::nm1].x_force[i*3+j] = velForceNodeData[iTurb][fast::n].x_force[i*3+j];
                 velForceNodeData[iTurb][fast::n].x_force[i*3+j] = velForceNodeData[iTurb][fast::np1].x_force[i*3+j];
                 velForceNodeData[iTurb][fast::nm1].xdot_force[i*3+j] = velForceNodeData[iTurb][fast::n].xdot_force[i*3+j];
                 velForceNodeData[iTurb][fast::n].xdot_force[i*3+j] = velForceNodeData[iTurb][fast::np1].xdot_force[i*3+j];
                 velForceNodeData[iTurb][fast::nm1].vel_force[i*3+j] = velForceNodeData[iTurb][fast::n].vel_force[i*3+j];
                 velForceNodeData[iTurb][fast::n].vel_force[i*3+j] = velForceNodeData[iTurb][fast::np1].vel_force[i*3+j];
                 velForceNodeData[iTurb][fast::nm1].force[i*3+j] = velForceNodeData[iTurb][fast::n].force[i*3+j];
                 velForceNodeData[iTurb][fast::n].force[i*3+j] = velForceNodeData[iTurb][fast::np1].force[i*3+j];
             }
             for (int j=0;j<9;j++) {
                 velForceNodeData[iTurb][fast::nm1].orient_force[i*9+j] = velForceNodeData[iTurb][fast::n].orient_force[i*9+j];
                 velForceNodeData[iTurb][fast::n].orient_force[i*9+j] = velForceNodeData[iTurb][fast::np1].orient_force[i*9+j];
             }
         }
     }
     
   }

   if(scStatus) {
     sc->updateStates(scInputsGlob); // Go from 'n' to 'n+1' based on input at previous time step
     fillScInputsGlob(); // Update inputs to super controller for 'n+1'
   }

   nt_global = nt_global + 1;
  
  if ( (((nt_global - ntStart) % nEveryCheckPoint) == 0 )  && (nt_global != ntStart) ) {
    //sprintf(CheckpointFileRoot, "../../CertTest/Test18.%d", nt_global);
    for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
      turbineData[iTurb].FASTRestartFileName = " "; // if blank, it will use FAST convention <RootName>.nt_global
      FAST_CreateCheckpoint(&iTurb, turbineData[iTurb].FASTRestartFileName.data(), &ErrStat, ErrMsg);
      checkError(ErrStat, ErrMsg);
    }
    if(scStatus) {
      if (fastMPIRank == 0) {
          sc->writeRestartFile(nt_global);
      }
    }
  }

}

void fast::OpenFAST::step() {

  /* ******************************
     set inputs from this code and call FAST:
  ********************************* */

   if(scStatus) {
     sc->calcOutputs(scOutputsGlob);
     fillScOutputsLoc();
   }

   for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {

     //  set wind speeds at original locations 
     //     setOutputsToFAST(i_f_FAST[iTurb], o_t_FAST[iTurb]);
	 
     // this advances the states, calls CalcOutput, and solves for next inputs. Predictor-corrector loop is imbeded here:
     // (note OpenFOAM could do subcycling around this step)

     if (turbineData[iTurb].inflowType == 2)
         writeVelocityData(velNodeDataFile, iTurb, nt_global, i_f_FAST[iTurb], o_t_FAST[iTurb]);
   
     if ( isDebug() && (turbineData[iTurb].inflowType == 2) ) {

         std::ofstream fastcpp_velocity_file;
         fastcpp_velocity_file.open("fastcpp_velocity.csv") ;
         fastcpp_velocity_file << "# x, y, z, Vx, Vy, Vz" << std::endl ;
         for (int iNode=0; iNode < get_numVelPtsLoc(iTurb); iNode++) {
             fastcpp_velocity_file << i_f_FAST[iTurb].pxVel[iNode] << ", " << i_f_FAST[iTurb].pyVel[iNode] << ", " << i_f_FAST[iTurb].pzVel[iNode] << ", " << o_t_FAST[iTurb].u[iNode] << ", " << o_t_FAST[iTurb].v[iNode] << ", " << o_t_FAST[iTurb].w[iNode] << " " << std::endl ;           
         }
         fastcpp_velocity_file.close() ;
     }
     
     FAST_OpFM_Prework(&iTurb, &ErrStat, ErrMsg);
     FAST_OpFM_UpdateStates(&iTurb, &ErrStat, ErrMsg);
     FAST_OpFM_AdvanceToNextTimeStep(&iTurb, &ErrStat, ErrMsg);
     checkError(ErrStat, ErrMsg);

     if ( isDebug() && (turbineData[iTurb].inflowType == 2) ) {
         std::ofstream actuatorForcesFile;
         actuatorForcesFile.open("actuator_forces.csv") ;
         actuatorForcesFile << "# x, y, z, fx, fy, fz" << std::endl ;
         for (int iNode=0; iNode < get_numForcePtsLoc(iTurb); iNode++) {
             actuatorForcesFile << i_f_FAST[iTurb].pxForce[iNode] << ", " << i_f_FAST[iTurb].pyForce[iNode] << ", " << i_f_FAST[iTurb].pzForce[iNode] << ", " << i_f_FAST[iTurb].fx[iNode] << ", " << i_f_FAST[iTurb].fy[iNode] << ", " << i_f_FAST[iTurb].fz[iNode] << " " << std::endl ;           
         }
         actuatorForcesFile.close() ;
     }
     
   }

   if(scStatus) {
     sc->updateStates(scInputsGlob); // Go from 'n' to 'n+1' based on input at previous time step
     fillScInputsGlob(); // Update inputs to super controller for 'n+1'
   }

   nt_global = nt_global + 1;
  
  if ( (((nt_global - ntStart) % nEveryCheckPoint) == 0 )  && (nt_global != ntStart) ) {
    //sprintf(FASTRestartFileName, "../../CertTest/Test18.%d", nt_global);
    for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
      turbineData[iTurb].FASTRestartFileName = " "; // if blank, it will use FAST convention <RootName>.nt_global
      FAST_CreateCheckpoint(&iTurb, turbineData[iTurb].FASTRestartFileName.data(), &ErrStat, ErrMsg);
      checkError(ErrStat, ErrMsg);
    }
    if(scStatus) {
      if (fastMPIRank == 0) {
          sc->writeRestartFile(nt_global);
      }
    }
  }

}

void fast::OpenFAST::stepNoWrite() {

  /* ******************************
     set inputs from this code and call FAST:
  ********************************* */

   if(scStatus) {
     sc->calcOutputs(scOutputsGlob);
     fillScOutputsLoc();
   }

   for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {

     //  set wind speeds at original locations 
     //     setOutputsToFAST(i_f_FAST[iTurb], o_t_FAST[iTurb]);

     // this advances the states, calls CalcOutput, and solves for next inputs. Predictor-corrector loop is imbeded here:
     // (note OpenFOAM could do subcycling around this step)
     FAST_OpFM_Step(&iTurb, &ErrStat, ErrMsg);
     checkError(ErrStat, ErrMsg);

   }

   if(scStatus) {
     sc->updateStates(scInputsGlob); // Go from 'n' to 'n+1' based on input at previous time step
     fillScInputsGlob(); // Update inputs to super controller for 'n+1'
   }

   nt_global = nt_global + 1;
  
}

void fast::OpenFAST::setInputs(const fast::fastInputs & fi ) {


  mpiComm = fi.comm;

  MPI_Comm_rank(mpiComm, &worldMPIRank);
  MPI_Comm_group(mpiComm, &worldMPIGroup);

  nTurbinesGlob = fi.nTurbinesGlob;

    if (nTurbinesGlob > 0) {
      
      dryRun = fi.dryRun;
      
      debug = fi.debug;

      tStart = fi.tStart;
      simStart = fi.simStart;
      nEveryCheckPoint = fi.nEveryCheckPoint;
      tMax = fi.tMax;
      loadSuperController(fi);
      dtFAST = fi.dtFAST;
      
      ntStart = int(tStart/dtFAST);
      
      if (simStart == fast::restartDriverInitFAST) {
	nt_global = 0;
      } else {
	nt_global = ntStart;
      }
      
      globTurbineData.resize(nTurbinesGlob);
      globTurbineData = fi.globTurbineData;

    } else {
      throw std::runtime_error("Number of turbines < 0 ");
    }
    
}

void fast::OpenFAST::checkError(const int ErrStat, const char * ErrMsg){

   if (ErrStat != ErrID_None){

      if (ErrStat >= AbortErrLev){
	throw std::runtime_error(ErrMsg);
      }

   }

}

void fast::OpenFAST::setOutputsToFAST(OpFM_InputType_t i_f_FAST, OpFM_OutputType_t o_t_FAST){

   // routine sets the u-v-w wind speeds used in FAST and the SuperController inputs

   for (int j = 0; j < o_t_FAST.u_Len; j++){
      o_t_FAST.u[j] = (float) 10.0*pow((i_f_FAST.pzVel[j] / 90.0), 0.2); // 0.2 power law wind profile using reference 10 m/s at 90 meters
      o_t_FAST.v[j] = 0.0;
      o_t_FAST.w[j] = 0.0;
   }

   // // call supercontroller
   // for (int j = 0; j < o_t_FAST.SuperController_Len; j++){
   //    o_t_FAST.SuperController[j] = (float) j; // set it somehow.... (would be set from the SuperController outputs)
   // }

}

void fast::OpenFAST::getApproxHubPos(std::vector<double> & currentCoords, int iTurbGlob) {

  // Get hub position of Turbine 'iTurbGlob'
  currentCoords[0] = turbineData[iTurbGlob].TurbineHubPos[0];
  currentCoords[1] = turbineData[iTurbGlob].TurbineHubPos[1];
  currentCoords[2] = turbineData[iTurbGlob].TurbineHubPos[2];

}

void fast::OpenFAST::getHubPos(std::vector<double> & currentCoords, int iTurbGlob, fast::timeStep t) {

  // Get hub position of Turbine 'iTurbGlob'
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  currentCoords[0] = velForceNodeData[iTurbLoc][t].x_force[0] + turbineData[iTurbLoc].TurbineBasePos[0] ;
  currentCoords[1] = velForceNodeData[iTurbLoc][t].x_force[1] + turbineData[iTurbLoc].TurbineBasePos[1] ;
  currentCoords[2] = velForceNodeData[iTurbLoc][t].x_force[2] + turbineData[iTurbLoc].TurbineBasePos[2] ;
  
}

void fast::OpenFAST::getHubShftDir(std::vector<double> & hubShftVec, int iTurbGlob, fast::timeStep t) {

  // Get hub shaft direction of current turbine - pointing downwind
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  hubShftVec[0] = velForceNodeData[iTurbLoc][t].orient_force[0] ;
  hubShftVec[1] = velForceNodeData[iTurbLoc][t].orient_force[3] ;
  hubShftVec[2] = velForceNodeData[iTurbLoc][t].orient_force[6] ;

}


void fast::OpenFAST::getVelNodeCoordinates(std::vector<double> & currentCoords, int iNode, int iTurbGlob, fast::timeStep t) {

  // Set coordinates at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numVelPtsLoc(iTurbLoc);
  currentCoords[0] = velForceNodeData[iTurbLoc][t].x_vel[iNode*3+0] + turbineData[iTurbLoc].TurbineBasePos[0] ;
  currentCoords[1] = velForceNodeData[iTurbLoc][t].x_vel[iNode*3+1] + turbineData[iTurbLoc].TurbineBasePos[1] ;
  currentCoords[2] = velForceNodeData[iTurbLoc][t].x_vel[iNode*3+2] + turbineData[iTurbLoc].TurbineBasePos[2] ;
  
}

void fast::OpenFAST::getForceNodeCoordinates(std::vector<double> & currentCoords, int iNode, int iTurbGlob, fast::timeStep t) {

  // Set coordinates at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  currentCoords[0] = velForceNodeData[iTurbLoc][t].x_force[iNode*3+0] + turbineData[iTurbLoc].TurbineBasePos[0] ;
  currentCoords[1] = velForceNodeData[iTurbLoc][t].x_force[iNode*3+1] + turbineData[iTurbLoc].TurbineBasePos[1] ;
  currentCoords[2] = velForceNodeData[iTurbLoc][t].x_force[iNode*3+2] + turbineData[iTurbLoc].TurbineBasePos[2] ;

}

void fast::OpenFAST::getForceNodeOrientation(std::vector<double> & currentOrientation, int iNode, int iTurbGlob, fast::timeStep t) {

  // Set orientation at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numForcePtsLoc(iTurbLoc);
  for(int i=0;i<9;i++) {
      currentOrientation[i] = velForceNodeData[iTurbLoc][t].orient_force[iNode*9+i] ;
  }

}

void fast::OpenFAST::getForce(std::vector<double> & currentForce, int iNode, int iTurbGlob, fast::timeStep t) {

  // Set forces at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numForcePtsLoc(iTurbLoc);
  currentForce[0] = -velForceNodeData[iTurbLoc][t].force[iNode*3+0] ;
  currentForce[1] = -velForceNodeData[iTurbLoc][t].force[iNode*3+1] ;
  currentForce[2] = -velForceNodeData[iTurbLoc][t].force[iNode*3+2] ;

}

double fast::OpenFAST::getChord(int iNode, int iTurbGlob) {

  // Return blade chord/tower diameter at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numForcePtsLoc(iTurbLoc);
  return i_f_FAST[iTurbLoc].forceNodesChord[iNode] ;

}

void fast::OpenFAST::setVelocity(std::vector<double> & currentVelocity, int iNode, int iTurbGlob) {

  // Set velocity at current node of current turbine - 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numVelPtsLoc(iTurbLoc);
  for(int k=0; k < 3; k++) {
      velForceNodeData[iTurbLoc][fast::np1].vel_vel_resid += (velForceNodeData[iTurbLoc][fast::np1].vel_vel[iNode*3+k] - currentVelocity[k])*(velForceNodeData[iTurbLoc][fast::np1].vel_vel[iNode*3+k] - currentVelocity[k]);
      velForceNodeData[iTurbLoc][fast::np1].vel_vel[iNode*3+k] = currentVelocity[k];
  }

  // Put this in send_data_to_openfast
  o_t_FAST[iTurbLoc].u[iNode] = currentVelocity[0];
  o_t_FAST[iTurbLoc].v[iNode] = currentVelocity[1];
  o_t_FAST[iTurbLoc].w[iNode] = currentVelocity[2];
}

void fast::OpenFAST::setVelocityForceNode(std::vector<double> & currentVelocity, int iNode, int iTurbGlob) {

  // Set velocity at current node of current turbine - 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numForcePtsLoc(iTurbLoc);
  for(int k=0; k < 3; k++) {
      velForceNodeData[iTurbLoc][fast::np1].vel_force_resid += (velForceNodeData[iTurbLoc][fast::np1].vel_force[iNode*3+k] - currentVelocity[k])*(velForceNodeData[iTurbLoc][fast::np1].vel_force[iNode*3+k] - currentVelocity[k]);
      velForceNodeData[iTurbLoc][fast::np1].vel_force[iNode*3+k] = currentVelocity[k];
  }
}

void fast::OpenFAST::interpolateVel_ForceToVelNodes() {

  // Interpolates the velocity from the force nodes to the velocity nodes
  
  for(int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
    // Hub location
      
    for (int k=0; k < 3; k++) {
        double tmp = velForceNodeData[iTurb][fast::np1].vel_force[k];
        velForceNodeData[iTurb][fast::np1].vel_vel_resid += (velForceNodeData[iTurb][fast::np1].vel_vel[k] - tmp)*(velForceNodeData[iTurb][fast::np1].vel_vel[k] - tmp);
        velForceNodeData[iTurb][fast::np1].vel_vel[k] = tmp;
    }
      
    if ( isDebug() ) {
       std::ofstream actuatorVelFile;
       actuatorVelFile.open("actuator_velocity.csv") ;
       actuatorVelFile << "# x, y, z, Vx, Vy, Vz" << std::endl ;
       for (int iNode=0; iNode < get_numForcePtsLoc(iTurb); iNode++) {
	 actuatorVelFile << i_f_FAST[iTurb].pxForce[iNode] << ", " << i_f_FAST[iTurb].pyForce[iNode] << ", " << i_f_FAST[iTurb].pzForce[iNode] << ", " << forceNodeVel[iTurb][iNode][0] << ", " << forceNodeVel[iTurb][iNode][1] << ", " << forceNodeVel[iTurb][iNode][2] << " " << std::endl ;
       }
       actuatorVelFile.close() ;
    }

    // Do the blades first
    int nBlades = get_numBladesLoc(iTurb);
    for(int iBlade=0; iBlade < nBlades; iBlade++) {

      // Create interpolating parameter - Distance from hub
      int nForcePtsBlade = get_numForcePtsBladeLoc(iTurb);
      std::vector<double> rDistForce(nForcePtsBlade) ;
      for(int j=0; j < nForcePtsBlade; j++) {
	int iNodeForce = 1 + iBlade * nForcePtsBlade + j ; //The number of actuator force points is always the same for all blades
	rDistForce[j] = sqrt( 
                             (i_f_FAST[iTurb].pxForce[iNodeForce] - i_f_FAST[iTurb].pxForce[0])*(i_f_FAST[iTurb].pxForce[iNodeForce] - i_f_FAST[iTurb].pxForce[0])  
		           + (i_f_FAST[iTurb].pyForce[iNodeForce] - i_f_FAST[iTurb].pyForce[0])*(i_f_FAST[iTurb].pyForce[iNodeForce] - i_f_FAST[iTurb].pyForce[0])  
		           + (i_f_FAST[iTurb].pzForce[iNodeForce] - i_f_FAST[iTurb].pzForce[0])*(i_f_FAST[iTurb].pzForce[iNodeForce] - i_f_FAST[iTurb].pzForce[0])  			
			    );
      }

      // Interpolate to the velocity nodes
      int nVelPtsBlade = get_numVelPtsBladeLoc(iTurb);
      for(int j=0; j < nVelPtsBlade; j++) {
	int iNodeVel = 1 + iBlade * nVelPtsBlade + j ; //Assumes the same number of velocity (Aerodyn) nodes for all blades
	double rDistVel = sqrt( 
			      (i_f_FAST[iTurb].pxVel[iNodeVel] - i_f_FAST[iTurb].pxVel[0])*(i_f_FAST[iTurb].pxVel[iNodeVel] - i_f_FAST[iTurb].pxVel[0])  
		            + (i_f_FAST[iTurb].pyVel[iNodeVel] - i_f_FAST[iTurb].pyVel[0])*(i_f_FAST[iTurb].pyVel[iNodeVel] - i_f_FAST[iTurb].pyVel[0])  
		            + (i_f_FAST[iTurb].pzVel[iNodeVel] - i_f_FAST[iTurb].pzVel[0])*(i_f_FAST[iTurb].pzVel[iNodeVel] - i_f_FAST[iTurb].pzVel[0])  			
			      );
	//Find nearest two force nodes
	int jForceLower = 0;
	while ( (rDistForce[jForceLower+1] < rDistVel) && ( jForceLower < (nForcePtsBlade-2)) )   {
	  jForceLower = jForceLower + 1;
	}
	int iNodeForceLower = 1 + iBlade * nForcePtsBlade + jForceLower ; 
	double rInterp = (rDistVel - rDistForce[jForceLower])/(rDistForce[jForceLower+1]-rDistForce[jForceLower]);
        for (int k=0; k < 3; k++) {
            double tmp = velForceNodeData[iTurb][fast::np1].vel_force[iNodeForceLower*3+k] + rInterp * (velForceNodeData[iTurb][fast::np1].vel_force[(iNodeForceLower+1)*3+k] - velForceNodeData[iTurb][fast::np1].vel_force[iNodeForceLower*3+k]);
            velForceNodeData[iTurb][fast::np1].vel_vel_resid += (velForceNodeData[iTurb][fast::np1].vel_vel[iNodeVel*3+k] - tmp)*(velForceNodeData[iTurb][fast::np1].vel_vel[iNodeVel*3+k] - tmp);
            velForceNodeData[iTurb][fast::np1].vel_vel[iNodeVel*3+k] = tmp;
        }
      }
    }

    // Now the tower if present and used
    int nVelPtsTower = get_numVelPtsTwrLoc(iTurb);
    if ( nVelPtsTower > 0 ) {

      // Create interpolating parameter - Distance from first node from ground
      int nForcePtsTower = get_numForcePtsTwrLoc(iTurb);
      std::vector<double> hDistForce(nForcePtsTower) ;
      int iNodeBotTowerForce = 1 + nBlades * get_numForcePtsBladeLoc(iTurb); // The number of actuator force points is always the same for all blades
      for(int j=0; j < nForcePtsTower; j++) {
	int iNodeForce = iNodeBotTowerForce + j ; 
	hDistForce[j] = sqrt( 
			     (i_f_FAST[iTurb].pxForce[iNodeForce] - i_f_FAST[iTurb].pxForce[iNodeBotTowerForce])*(i_f_FAST[iTurb].pxForce[iNodeForce] - i_f_FAST[iTurb].pxForce[iNodeBotTowerForce])  
                           + (i_f_FAST[iTurb].pyForce[iNodeForce] - i_f_FAST[iTurb].pyForce[iNodeBotTowerForce])*(i_f_FAST[iTurb].pyForce[iNodeForce] - i_f_FAST[iTurb].pyForce[iNodeBotTowerForce])  
			   + (i_f_FAST[iTurb].pzForce[iNodeForce] - i_f_FAST[iTurb].pzForce[iNodeBotTowerForce])*(i_f_FAST[iTurb].pzForce[iNodeForce] - i_f_FAST[iTurb].pzForce[iNodeBotTowerForce])	
			    );
      }
      
      
      int iNodeBotTowerVel = 1 + nBlades * get_numVelPtsBladeLoc(iTurb); // Assumes the same number of velocity (Aerodyn) nodes for all blades
      for(int j=0; j < nVelPtsTower; j++) {
	int iNodeVel = iNodeBotTowerVel + j ; 
	double hDistVel = sqrt( 
			       (i_f_FAST[iTurb].pxVel[iNodeVel] - i_f_FAST[iTurb].pxVel[iNodeBotTowerVel])*(i_f_FAST[iTurb].pxVel[iNodeVel] - i_f_FAST[iTurb].pxVel[iNodeBotTowerVel])  
                             + (i_f_FAST[iTurb].pyVel[iNodeVel] - i_f_FAST[iTurb].pyVel[iNodeBotTowerVel])*(i_f_FAST[iTurb].pyVel[iNodeVel] - i_f_FAST[iTurb].pyVel[iNodeBotTowerVel])  
                             + (i_f_FAST[iTurb].pzVel[iNodeVel] - i_f_FAST[iTurb].pzVel[iNodeBotTowerVel])*(i_f_FAST[iTurb].pzVel[iNodeVel] - i_f_FAST[iTurb].pzVel[iNodeBotTowerVel])  			
	                      );
	//Find nearest two force nodes
	int jForceLower = 0;
	while ( (hDistForce[jForceLower+1] < hDistVel) && ( jForceLower < (nForcePtsTower-2)) )   {
	  jForceLower = jForceLower + 1;
	}
	int iNodeForceLower = iNodeBotTowerForce + jForceLower ; 
	double rInterp = (hDistVel - hDistForce[jForceLower])/(hDistForce[jForceLower+1]-hDistForce[jForceLower]);
        for (int k=0; k < 3; k++) {
            double tmp = velForceNodeData[iTurb][fast::np1].vel_force[iNodeForceLower*3+k] + rInterp * (velForceNodeData[iTurb][fast::np1].vel_force[(iNodeForceLower+1)*3+k] - velForceNodeData[iTurb][fast::np1].vel_force[iNodeForceLower*3+k]);
            velForceNodeData[iTurb][fast::np1].vel_vel_resid += (velForceNodeData[iTurb][fast::np1].vel_vel[iNodeVel*3+k] - tmp)*(velForceNodeData[iTurb][fast::np1].vel_vel[iNodeVel*3+k] - tmp);
            velForceNodeData[iTurb][fast::np1].vel_vel[iNodeVel*3+k] = tmp;
        }
      }
    }
    
  } // End loop over turbines
  
}

void fast::OpenFAST::computeTorqueThrust(int iTurbGlob, std::vector<double> & torque, std::vector<double> & thrust) {

    //Compute the torque and thrust based on the forces at the actuator nodes
    std::vector<double> relLoc(3,0.0);
    std::vector<double> rPerpShft(3);
    thrust[0] = 0.0; thrust[1] = 0.0; thrust[2] = 0.0;
    torque[0] = 0.0; torque[1] = 0.0; torque[2] = 0.0;    
    
    std::vector<double> hubShftVec(3);
    getHubShftDir(hubShftVec, iTurbGlob, fast::np1);

    int iTurbLoc = get_localTurbNo(iTurbGlob) ;
    int nfpts = get_numForcePtsBlade(iTurbLoc);
    for (int k=0; k < get_numBladesLoc(iTurbLoc); k++) {
        for (int j=0; j < nfpts; j++) {
            int iNode = 1 + nfpts*k + j ;
            
            thrust[0] = thrust[0] + i_f_FAST[iTurbLoc].fx[iNode] ;
            thrust[1] = thrust[1] + i_f_FAST[iTurbLoc].fy[iNode] ;
            thrust[2] = thrust[2] + i_f_FAST[iTurbLoc].fz[iNode] ;

            relLoc[0] = i_f_FAST[iTurbLoc].pxForce[iNode] - i_f_FAST[iTurbLoc].pxForce[0] ;
            relLoc[1] = i_f_FAST[iTurbLoc].pyForce[iNode] - i_f_FAST[iTurbLoc].pyForce[0];
            relLoc[2] = i_f_FAST[iTurbLoc].pzForce[iNode] - i_f_FAST[iTurbLoc].pzForce[0];            
	    
	    double rDotHubShftVec = relLoc[0]*hubShftVec[0] + relLoc[1]*hubShftVec[1] + relLoc[2]*hubShftVec[2]; 
	    for (int j=0; j < 3; j++)  rPerpShft[j] = relLoc[j] - rDotHubShftVec * hubShftVec[j];

            torque[0] = torque[0] + rPerpShft[1] * i_f_FAST[iTurbLoc].fz[iNode] - rPerpShft[2] * i_f_FAST[iTurbLoc].fy[iNode] + i_f_FAST[iTurbLoc].momentx[iNode] ;
            torque[1] = torque[1] + rPerpShft[2] * i_f_FAST[iTurbLoc].fx[iNode] - rPerpShft[0] * i_f_FAST[iTurbLoc].fz[iNode] + i_f_FAST[iTurbLoc].momenty[iNode] ;
            torque[2] = torque[2] + rPerpShft[0] * i_f_FAST[iTurbLoc].fy[iNode] - rPerpShft[1] * i_f_FAST[iTurbLoc].fx[iNode] + i_f_FAST[iTurbLoc].momentz[iNode] ;
            
        }
    }
}

fast::ActuatorNodeType fast::OpenFAST::getVelNodeType(int iTurbGlob, int iNode) {
  // Return the type of velocity node for the given node number. The node ordering (from FAST) is 
  // Node 0 - Hub node
  // Blade 1 nodes
  // Blade 2 nodes
  // Blade 3 nodes
  // Tower nodes

  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numVelPtsLoc(iTurbGlob);
  if (iNode) {
    if ( (iNode + 1 - (get_numVelPts(iTurbLoc) - get_numVelPtsTwr(iTurbLoc)) ) > 0) {
      return TOWER; 
    }
    else {
      return BLADE;
    }
  }
  else {
    return HUB; 
  }
  
}

fast::ActuatorNodeType fast::OpenFAST::getForceNodeType(int iTurbGlob, int iNode) {
  // Return the type of actuator force node for the given node number. The node ordering (from FAST) is 
  // Node 0 - Hub node
  // Blade 1 nodes
  // Blade 2 nodes
  // Blade 3 nodes
  // Tower nodes

  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numForcePtsLoc(iTurbGlob);
  if (iNode) {
    if ( (iNode + 1 - (get_numForcePts(iTurbLoc) - get_numForcePtsTwr(iTurbLoc)) ) > 0) {
      return TOWER; 
    }
    else {
      return BLADE;
    }
  }
  else {
    return HUB; 
  }
  
}

void fast::OpenFAST::allocateMemory() {
	
  for (int iTurb=0; iTurb < nTurbinesGlob; iTurb++) {
    if (dryRun) {
      if(worldMPIRank == 0) {
	std::cout << "iTurb = " << iTurb << " turbineMapGlobToProc[iTurb] = " << turbineMapGlobToProc[iTurb] << std::endl ;
      }
    }
    if(worldMPIRank == turbineMapGlobToProc[iTurb]) {
      turbineMapProcToGlob[nTurbinesProc] = iTurb;
      reverseTurbineMapProcToGlob[iTurb] = nTurbinesProc;
      nTurbinesProc++ ;
    }
    turbineSetProcs.insert(turbineMapGlobToProc[iTurb]);
  }

  int nProcsWithTurbines=0;
  turbineProcs.resize(turbineSetProcs.size());

  for (std::set<int>::const_iterator p = turbineSetProcs.begin(); p != turbineSetProcs.end(); p++) {
    turbineProcs[nProcsWithTurbines] = *p;
    nProcsWithTurbines++ ;
  }

  if (dryRun) {
    if (nTurbinesProc > 0) {
      std::ofstream turbineAllocFile;
      turbineAllocFile.open("turbineAlloc." + std::to_string(worldMPIRank) + ".txt") ;
      for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
	turbineAllocFile << "Proc " << worldMPIRank << " loc iTurb " << iTurb << " glob iTurb " << turbineMapProcToGlob[iTurb] << std::endl ;
      }
      turbineAllocFile.flush();
      turbineAllocFile.close() ;
    }
  
  }

    
  // Construct a group containing all procs running atleast 1 turbine in FAST
  MPI_Group_incl(worldMPIGroup, nProcsWithTurbines, &turbineProcs[0], &fastMPIGroup) ;
  int fastMPIcommTag = MPI_Comm_create(mpiComm, fastMPIGroup, &fastMPIComm);
  if (MPI_COMM_NULL != fastMPIComm) {
    MPI_Comm_rank(fastMPIComm, &fastMPIRank);
  }

  turbineData.resize(nTurbinesProc);
  forceNodeVel.resize(nTurbinesProc);
  velForceNodeData.resize(nTurbinesProc);
  
  for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
    
    turbineData[iTurb].TurbineBasePos.resize(3);

    int iTurbGlob = turbineMapProcToGlob[iTurb];
    turbineData[iTurb].TurbID = globTurbineData[iTurbGlob].TurbID;
    turbineData[iTurb].FASTInputFileName = globTurbineData[iTurbGlob].FASTInputFileName ;
    turbineData[iTurb].FASTRestartFileName = globTurbineData[iTurbGlob].FASTRestartFileName ;
    for(int i=0;i<3;i++)
        turbineData[iTurb].TurbineBasePos[i] = globTurbineData[iTurbGlob].TurbineBasePos[i];
    turbineData[iTurb].numForcePtsBlade = globTurbineData[iTurbGlob].numForcePtsBlade;
    turbineData[iTurb].numForcePtsTwr = globTurbineData[iTurbGlob].numForcePtsTwr;

    velForceNodeData[iTurb].resize(3); // To hold data for 3 time steps

  }

  // Allocate memory for Turbine datastructure for all turbines
  FAST_AllocateTurbines(&nTurbinesProc, &ErrStat, ErrMsg);
  
  // Allocate memory for OpFM Input types in FAST
  i_f_FAST.resize(nTurbinesProc) ;
  o_t_FAST.resize(nTurbinesProc) ;
  
  sc_i_f_FAST.resize(nTurbinesProc) ;
  sc_o_t_FAST.resize(nTurbinesProc) ;

}

void fast::OpenFAST::allocateMemory2(int iTurbLoc) {

       if ( turbineData[iTurbLoc].inflowType == 1) {
           // Inflow data is coming from inflow module
           turbineData[iTurbLoc].numForcePtsTwr = 0;
           turbineData[iTurbLoc].numForcePtsBlade = 0;
           turbineData[iTurbLoc].numForcePts = 0;
           turbineData[iTurbLoc].numVelPtsTwr = 0;
           turbineData[iTurbLoc].numVelPtsBlade = 0;
           turbineData[iTurbLoc].numVelPts = 0;
       } else {
           //Inflow data is coming from external program like a CFD solver
           turbineData[iTurbLoc].numForcePts = 1 + turbineData[iTurbLoc].numForcePtsTwr + turbineData[iTurbLoc].numBlades * turbineData[iTurbLoc].numForcePtsBlade ;
           turbineData[iTurbLoc].numVelPts = 1 + turbineData[iTurbLoc].numVelPtsTwr + turbineData[iTurbLoc].numBlades * turbineData[iTurbLoc].numVelPtsBlade ;

           int nfpts = get_numForcePtsLoc(iTurbLoc);
           forceNodeVel[iTurbLoc].resize(nfpts);

           int nvelpts = get_numVelPtsLoc(iTurbLoc);
           for (int k = 0; k < nfpts; k++) forceNodeVel[iTurbLoc][k].resize(3) ;

           for(int k=0; k<3; k++) {
               velForceNodeData[iTurbLoc][k].x_vel.resize(3*nvelpts) ;
               velForceNodeData[iTurbLoc][k].xdot_vel.resize(3*nvelpts) ;
               velForceNodeData[iTurbLoc][k].vel_vel.resize(3*nvelpts) ;
               velForceNodeData[iTurbLoc][k].x_force.resize(3*nfpts) ;
               velForceNodeData[iTurbLoc][k].xdot_force.resize(3*nfpts) ;
               velForceNodeData[iTurbLoc][k].orient_force.resize(9*nfpts) ;
               velForceNodeData[iTurbLoc][k].vel_force.resize(3*nfpts) ;
               velForceNodeData[iTurbLoc][k].force.resize(3*nfpts) ;
           }

           if ( isDebug() ) {
               for (int iNode=0; iNode < get_numVelPtsLoc(iTurbLoc); iNode++) {
                   std::cout << "Node " << iNode << " Position = " << i_f_FAST[iTurbLoc].pxVel[iNode] << " " << i_f_FAST[iTurbLoc].pyVel[iNode] << " " << i_f_FAST[iTurbLoc].pzVel[iNode] << " " << std::endl ;
               }
           }
       }
}

void fast::OpenFAST::allocateTurbinesToProcsSimple() {
  
  // Allocate turbines to each processor - round robin fashion
  int nProcs ;
  MPI_Comm_size(mpiComm, &nProcs);
  for(int j = 0; j < nTurbinesGlob; j++)  turbineMapGlobToProc[j] = j % nProcs ;
  
}

void fast::OpenFAST::end() {

  // Deallocate types we allocated earlier
  
  if (nTurbinesProc > 0) closeVelocityDataFile(nt_global, velNodeDataFile);
  
  if ( !dryRun) {
    bool stopTheProgram = false;
    for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
      FAST_End(&iTurb, &stopTheProgram);
    }
  }
  
  MPI_Group_free(&fastMPIGroup);
  if (MPI_COMM_NULL != fastMPIComm) {
    MPI_Comm_free(&fastMPIComm);
  }
  MPI_Group_free(&worldMPIGroup);
  
  if(scStatus) {
    
    destroy_SuperController(sc) ;
    
    if(scLibHandle != NULL) {
      // close the library
      std::cout << "Closing library...\n";
      dlclose(scLibHandle);
    }
    
  }
  
}

void fast::OpenFAST::readVelocityData(int nTimesteps) {
  
  int nTurbines;
  
  hid_t velDataFile = H5Fopen(("velDatafile." + std::to_string(worldMPIRank) + ".h5").c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  
  {
    hid_t attr = H5Aopen(velDataFile, "nTurbines", H5P_DEFAULT);
    herr_t ret = H5Aread(attr, H5T_NATIVE_INT, &nTurbines) ;
    H5Aclose(attr);

  }

  // Allocate memory and read the velocity data. 
  velNodeData.resize(nTurbines);
  for (int iTurb=0; iTurb < nTurbines; iTurb++) {
    int nVelPts = get_numVelPtsLoc(iTurb) ;
    velNodeData[iTurb].resize(nTimesteps*nVelPts*6) ;
    hid_t dset_id = H5Dopen2(velDataFile, ("/turbine" + std::to_string(iTurb)).c_str(), H5P_DEFAULT);
    hid_t dspace_id = H5Dget_space(dset_id);

    hsize_t start[3]; start[1] = 0; start[2] = 0;
    hsize_t count[3]; count[0] = 1; count[1] = nVelPts; count[2] = 6;
    hid_t mspace_id = H5Screate_simple(3, count, NULL); 

    for (int iStep=0; iStep < nTimesteps; iStep++) {
      start[0] = iStep;
      H5Sselect_hyperslab(dspace_id, H5S_SELECT_SET, start, NULL, count, NULL);
      herr_t status = H5Dread(dset_id, H5T_NATIVE_DOUBLE, mspace_id, dspace_id, H5P_DEFAULT, &velNodeData[iTurb][iStep*nVelPts*6] );
    }
    herr_t status = H5Dclose(dset_id);


  }
  
}

hid_t fast::OpenFAST::openVelocityDataFile(bool createFile) {

  hid_t velDataFile;
  if (createFile) {
    // Open the file in create mode
    velDataFile = H5Fcreate(("velDatafile." + std::to_string(worldMPIRank) + ".h5").c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

    {
      hsize_t dims[1];
      dims[0] = 1;
      hid_t dataSpace = H5Screate_simple(1, dims, NULL);
      hid_t attr = H5Acreate2(velDataFile, "nTurbines", H5T_NATIVE_INT, dataSpace, H5P_DEFAULT, H5P_DEFAULT) ;
      herr_t status = H5Awrite(attr, H5T_NATIVE_INT, &nTurbinesProc);
      status = H5Aclose(attr);
      status = H5Sclose(dataSpace);
      
      dataSpace = H5Screate_simple(1, dims, NULL);
      attr = H5Acreate2(velDataFile, "nTimesteps", H5T_NATIVE_INT, dataSpace, H5P_DEFAULT, H5P_DEFAULT) ;
      status = H5Aclose(attr);
      status = H5Sclose(dataSpace);
    }

    int ntMax = tMax/dtFAST ;

    for (int iTurb = 0; iTurb < nTurbinesProc; iTurb++) {
      int nVelPts = get_numVelPtsLoc(iTurb);
      hsize_t dims[3];
      dims[0] = ntMax; dims[1] = nVelPts; dims[2] = 6 ;

      hsize_t chunk_dims[3];
      chunk_dims[0] = 1; chunk_dims[1] = nVelPts; chunk_dims[2] = 6;
      hid_t dcpl_id = H5Pcreate(H5P_DATASET_CREATE);
      H5Pset_chunk(dcpl_id, 3, chunk_dims);

      hid_t dataSpace = H5Screate_simple(3, dims, NULL);
      hid_t dataSet = H5Dcreate(velDataFile, ("/turbine" + std::to_string(iTurb)).c_str(), H5T_NATIVE_DOUBLE, dataSpace, H5P_DEFAULT, dcpl_id, H5P_DEFAULT);    

      herr_t status = H5Pclose(dcpl_id);
      status = H5Dclose(dataSet);
      status = H5Sclose(dataSpace);
    }
    
  } else {
    // Open the file in append mode
    velDataFile = H5Fopen(("velDatafile." + std::to_string(worldMPIRank) + ".h5").c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  }

  return velDataFile;

}

herr_t fast::OpenFAST::closeVelocityDataFile(int nt_global, hid_t velDataFile) {

  herr_t status = H5Fclose(velDataFile) ;
  return status;
}


void fast::OpenFAST::writeVelocityData(hid_t h5File, int iTurb, int iTimestep, OpFM_InputType_t iData, OpFM_OutputType_t oData) {

    hsize_t start[3]; start[0] = iTimestep; start[1] = 0; start[2] = 0;
    int nVelPts = get_numVelPtsLoc(iTurb) ;
    hsize_t count[3]; count[0] = 1; count[1] = nVelPts; count[2] = 6;
    
    std::vector<double> tmpVelData;
    tmpVelData.resize(nVelPts * 6);
    
    for (int iNode=0 ; iNode < nVelPts; iNode++) {
        tmpVelData[iNode*6 + 0] = iData.pxVel[iNode];
        tmpVelData[iNode*6 + 1] = iData.pyVel[iNode];
        tmpVelData[iNode*6 + 2] = iData.pzVel[iNode];
        tmpVelData[iNode*6 + 3] = oData.u[iNode];
        tmpVelData[iNode*6 + 4] = oData.v[iNode];
        tmpVelData[iNode*6 + 5] = oData.w[iNode];
    }
    
    hid_t dset_id = H5Dopen2(h5File, ("/turbine" + std::to_string(iTurb)).c_str(), H5P_DEFAULT);
    hid_t dspace_id = H5Dget_space(dset_id);
    H5Sselect_hyperslab(dspace_id, H5S_SELECT_SET, start, NULL, count, NULL);
    hid_t mspace_id = H5Screate_simple(3, count, NULL);  
    H5Dwrite(dset_id, H5T_NATIVE_DOUBLE, mspace_id, dspace_id, H5P_DEFAULT, tmpVelData.data());
    
    H5Dclose(dset_id);
    H5Sclose(dspace_id);
    H5Sclose(mspace_id);
    
    hid_t attr_id = H5Aopen_by_name(h5File, ".", "nTimesteps", H5P_DEFAULT, H5P_DEFAULT);
    herr_t status = H5Awrite(attr_id, H5T_NATIVE_INT, &iTimestep);
    status = H5Aclose(attr_id);
    
}
    
void fast::OpenFAST::applyVelocityData(int iPrestart, int iTurb, OpFM_OutputType_t o_t_FAST, std::vector<double> & velData) {

  int nVelPts = get_numVelPtsLoc(iTurb);
  for (int j = 0; j < nVelPts; j++){
    o_t_FAST.u[j] = velData[(iPrestart*nVelPts+j)*6 + 3]; 
    o_t_FAST.v[j] = velData[(iPrestart*nVelPts+j)*6 + 4];
    o_t_FAST.w[j] = velData[(iPrestart*nVelPts+j)*6 + 5];
   }

}

void fast::OpenFAST::send_data_to_openfast(fast::timeStep t) {

    for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
        if (turbineData[iTurb].inflowType == 2) {
            int nvelpts = get_numVelPtsLoc(iTurb);
            for (int iNodeVel=0; iNodeVel < nvelpts; iNodeVel++) {
                o_t_FAST[iTurb].u[iNodeVel] = velForceNodeData[iTurb][t].vel_vel[iNodeVel*3+0];
                o_t_FAST[iTurb].v[iNodeVel] = velForceNodeData[iTurb][t].vel_vel[iNodeVel*3+1];
                o_t_FAST[iTurb].w[iNodeVel] = velForceNodeData[iTurb][t].vel_vel[iNodeVel*3+2];
            }
        }
    }
}

void fast::OpenFAST::get_data_from_openfast(timeStep t) {

    for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
        if (turbineData[iTurb].inflowType == 2) {
            int nvelpts = get_numVelPtsLoc(iTurb);
            int nfpts = get_numForcePtsLoc(iTurb);
            for (int i=0; i<nvelpts; i++) {
                velForceNodeData[iTurb][t].x_vel_resid += (velForceNodeData[iTurb][t].x_vel[i*3+0] - i_f_FAST[iTurb].pxVel[i])*(velForceNodeData[iTurb][t].x_vel[i*3+0] - i_f_FAST[iTurb].pxVel[i]);
                velForceNodeData[iTurb][t].x_vel[i*3+0] = i_f_FAST[iTurb].pxVel[i];
                velForceNodeData[iTurb][t].x_vel_resid += (velForceNodeData[iTurb][t].x_vel[i*3+1] - i_f_FAST[iTurb].pyVel[i])*(velForceNodeData[iTurb][t].x_vel[i*3+1] - i_f_FAST[iTurb].pyVel[i]);
                velForceNodeData[iTurb][t].x_vel[i*3+1] = i_f_FAST[iTurb].pyVel[i];
                velForceNodeData[iTurb][t].x_vel_resid += (velForceNodeData[iTurb][t].x_vel[i*3+2] - i_f_FAST[iTurb].pzVel[i])*(velForceNodeData[iTurb][t].x_vel[i*3+2] - i_f_FAST[iTurb].pzVel[i]);
                velForceNodeData[iTurb][t].x_vel[i*3+2] = i_f_FAST[iTurb].pzVel[i];
                velForceNodeData[iTurb][t].xdot_vel_resid += (velForceNodeData[iTurb][t].xdot_vel[i*3+0] - i_f_FAST[iTurb].pxdotVel[i])*(velForceNodeData[iTurb][t].xdot_vel[i*3+0] - i_f_FAST[iTurb].pxdotVel[i]);
                velForceNodeData[iTurb][t].xdot_vel[i*3+0] = i_f_FAST[iTurb].pxdotVel[i];
                velForceNodeData[iTurb][t].xdot_vel_resid += (velForceNodeData[iTurb][t].xdot_vel[i*3+1] - i_f_FAST[iTurb].pydotVel[i])*(velForceNodeData[iTurb][t].xdot_vel[i*3+1] - i_f_FAST[iTurb].pydotVel[i]);
                velForceNodeData[iTurb][t].xdot_vel[i*3+1] = i_f_FAST[iTurb].pydotVel[i];
                velForceNodeData[iTurb][t].xdot_vel_resid += (velForceNodeData[iTurb][t].xdot_vel[i*3+2] - i_f_FAST[iTurb].pzdotVel[i])*(velForceNodeData[iTurb][t].xdot_vel[i*3+2] - i_f_FAST[iTurb].pzdotVel[i]);
                velForceNodeData[iTurb][t].xdot_vel[i*3+2] = i_f_FAST[iTurb].pzdotVel[i];
            }

            for (int i=0; i<nfpts; i++) {
                velForceNodeData[iTurb][t].x_force_resid += (velForceNodeData[iTurb][t].x_force[i*3+0] - i_f_FAST[iTurb].pxForce[i])*(velForceNodeData[iTurb][t].x_force[i*3+0] - i_f_FAST[iTurb].pxForce[i]);
                velForceNodeData[iTurb][t].x_force[i*3+0] = i_f_FAST[iTurb].pxForce[i];
                velForceNodeData[iTurb][t].x_force_resid += (velForceNodeData[iTurb][t].x_force[i*3+1] - i_f_FAST[iTurb].pyForce[i])*(velForceNodeData[iTurb][t].x_force[i*3+1] - i_f_FAST[iTurb].pyForce[i]);
                velForceNodeData[iTurb][t].x_force[i*3+1] = i_f_FAST[iTurb].pyForce[i];
                velForceNodeData[iTurb][t].x_force_resid += (velForceNodeData[iTurb][t].x_force[i*3+2] - i_f_FAST[iTurb].pzForce[i])*(velForceNodeData[iTurb][t].x_force[i*3+2] - i_f_FAST[iTurb].pzForce[i]);
                velForceNodeData[iTurb][t].x_force[i*3+2] = i_f_FAST[iTurb].pzForce[i];
                velForceNodeData[iTurb][t].xdot_force_resid += (velForceNodeData[iTurb][t].xdot_force[i*3+0] - i_f_FAST[iTurb].pxdotForce[i])*(velForceNodeData[iTurb][t].xdot_force[i*3+0] - i_f_FAST[iTurb].pxdotForce[i]);
                velForceNodeData[iTurb][t].xdot_force[i*3+0] = i_f_FAST[iTurb].pxdotForce[i];
                velForceNodeData[iTurb][t].xdot_force_resid += (velForceNodeData[iTurb][t].xdot_force[i*3+1] - i_f_FAST[iTurb].pydotForce[i])*(velForceNodeData[iTurb][t].xdot_force[i*3+1] - i_f_FAST[iTurb].pydotForce[i]);
                velForceNodeData[iTurb][t].xdot_force[i*3+1] = i_f_FAST[iTurb].pydotForce[i];
                velForceNodeData[iTurb][t].xdot_force_resid += (velForceNodeData[iTurb][t].xdot_force[i*3+2] - i_f_FAST[iTurb].pzdotForce[i])*(velForceNodeData[iTurb][t].xdot_force[i*3+2] - i_f_FAST[iTurb].pzdotForce[i]);
                velForceNodeData[iTurb][t].xdot_force[i*3+2] = i_f_FAST[iTurb].pzdotForce[i];
                for (int j=0;j<9;j++) {
                    velForceNodeData[iTurb][t].orient_force_resid += (velForceNodeData[iTurb][t].orient_force[i*9+j] - i_f_FAST[iTurb].pOrientation[i*9+j])*(velForceNodeData[iTurb][t].orient_force[i*9+j] - i_f_FAST[iTurb].pOrientation[i*9+j]);
                    velForceNodeData[iTurb][t].orient_force[i*9+j] = i_f_FAST[iTurb].pOrientation[i*9+j];                    
                }
                velForceNodeData[iTurb][t].force_resid += (velForceNodeData[iTurb][t].force[i*3+0] - i_f_FAST[iTurb].fx[i])*(velForceNodeData[iTurb][t].force[i*3+0] - i_f_FAST[iTurb].fx[i]);
                velForceNodeData[iTurb][t].force[i*3+0] = i_f_FAST[iTurb].fx[i];
                velForceNodeData[iTurb][t].force_resid += (velForceNodeData[iTurb][t].force[i*3+1] - i_f_FAST[iTurb].fy[i])*(velForceNodeData[iTurb][t].force[i*3+1] - i_f_FAST[iTurb].fy[i]);
                velForceNodeData[iTurb][t].force[i*3+1] = i_f_FAST[iTurb].fy[i];
                velForceNodeData[iTurb][t].force_resid += (velForceNodeData[iTurb][t].force[i*3+2] - i_f_FAST[iTurb].fz[i])*(velForceNodeData[iTurb][t].force[i*3+2] - i_f_FAST[iTurb].fz[i]);
                velForceNodeData[iTurb][t].force[i*3+2] = i_f_FAST[iTurb].fz[i];
            }
        }
    }
}

void fast::OpenFAST::loadSuperController(const fast::fastInputs & fi) {

  if(fi.scStatus) {

    scStatus = fi.scStatus;
    scLibFile = fi.scLibFile;

    // open the library
    scLibHandle = dlopen(scLibFile.c_str(), RTLD_LAZY);
    if (!scLibHandle) {
      std::cerr << "Cannot open library: " << dlerror() << '\n';
    }
    
    create_SuperController = (create_sc_t*) dlsym(scLibHandle, "create_sc");
    // reset errors
    dlerror();
    const char *dlsym_error = dlerror();
    if (dlsym_error) {
      std::cerr << "Cannot load symbol 'create_sc': " << dlsym_error << '\n';
      dlclose(scLibHandle);
    }

    destroy_SuperController = (destroy_sc_t*) dlsym(scLibHandle, "destroy_sc");
    // reset errors
    dlerror();
    const char *dlsym_error_us = dlerror();
    if (dlsym_error_us) {
      std::cerr << "Cannot load symbol 'destroy_sc': " << dlsym_error_us << '\n';
      dlclose(scLibHandle);
    }

    sc = create_SuperController() ;

    numScInputs = fi.numScInputs;
    numScOutputs = fi.numScOutputs;

    if ( (numScInputs > 0) && (numScOutputs > 0)) {
      scOutputsGlob.resize(nTurbinesGlob*numScOutputs) ;
      scInputsGlob.resize(nTurbinesGlob*numScInputs) ;
      for (int iTurb=0; iTurb < nTurbinesGlob; iTurb++) {
	for(int iInput=0; iInput < numScInputs; iInput++) {
	  scInputsGlob[iTurb*numScInputs + iInput] = 0.0 ; // Initialize to zero
	}
	for(int iOutput=0; iOutput < numScOutputs; iOutput++) {
	  scOutputsGlob[iTurb*numScOutputs + iOutput] = 0.0 ; // Initialize to zero
	}
      }

    } else {
      std::cerr <<  "Make sure numScInputs and numScOutputs are greater than zero" << std::endl;
    }
    
   } else {
    scStatus = false;
    numScInputs = 0;
    numScOutputs = 0;
   }

}

void fast::OpenFAST::fillScInputsGlob() {
  
  // Fills the global array containing inputs to the supercontroller from all turbines

  for(int iTurb=0; iTurb < nTurbinesGlob; iTurb++) {
    for(int iInput=0; iInput < numScInputs; iInput++) {
      scInputsGlob[iTurb*numScInputs + iInput] = 0.0; // Initialize to zero 
    }
  }
  
  for(int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
    for(int iInput=0; iInput < numScInputs; iInput++) {
      scInputsGlob[turbineMapProcToGlob[iTurb]*numScInputs + iInput] = sc_i_f_FAST[iTurb].toSC[iInput] ;
    }
  }
  
  if (MPI_COMM_NULL != fastMPIComm) {
    MPI_Allreduce(MPI_IN_PLACE, scInputsGlob.data(), numScInputs*nTurbinesGlob, MPI_DOUBLE, MPI_SUM, fastMPIComm) ;
  }

}


void fast::OpenFAST::fillScOutputsLoc() {
  
  // Fills the local array containing outputs from the supercontroller to each turbine
  
  for(int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
    for(int iOutput=0; iOutput < numScOutputs; iOutput++) {
      sc_o_t_FAST[iTurb].fromSC[iOutput] = scOutputsGlob[turbineMapProcToGlob[iTurb]*numScOutputs + iOutput] ;
    }
  }

}











