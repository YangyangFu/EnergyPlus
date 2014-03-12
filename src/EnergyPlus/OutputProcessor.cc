// C++ Headers
#include <cmath>
#include <string>

// ObjexxFCL Headers
#include <ObjexxFCL/FArray.functions.hh>
#include <ObjexxFCL/Fmath.hh>
#include <ObjexxFCL/gio.hh>

// EnergyPlus Headers
#include <OutputProcessor.hh>
#include <DataEnvironment.hh>
#include <DataGlobalConstants.hh>
#include <DataHeatBalance.hh>
#include <DataIPShortCuts.hh>
#include <DataOutputs.hh>
#include <DataPrecisionGlobals.hh>
#include <DataStringGlobals.hh>
#include <DataSystemVariables.hh>
#include <General.hh>
#include <InputProcessor.hh>
#include <OutputProcessor.hh>
#include <OutputReportPredefined.hh>
#include <ScheduleManager.hh>
#include <SortAndStringUtilities.hh>
#include <SQLiteProcedures.hh>
#include <UtilityRoutines.hh>

namespace EnergyPlus {

namespace OutputProcessor {

	// MODULE INFORMATION:
	//       AUTHOR         Linda Lawrie
	//       DATE WRITTEN   December 1998
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS MODULE:
	// This module contains the major Output Processor routines.
	// In addition, in this file are several routines which can be called
	// without Useing the OutputProcessor Module

	// METHODOLOGY EMPLOYED:
	// Lots of pointers and other fancy data stuff.

	// REFERENCES:
	// EnergyPlus OutputProcessor specifications.

	// OTHER NOTES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using DataGlobals::MaxNameLength;
	using DataGlobals::OutputFileMeters;
	using DataGlobals::HourOfDay;
	using DataGlobals::DayOfSim;
	using DataGlobals::DayOfSimChr;
	using DataGlobals::OutputFileStandard;
	using DataGlobals::MinutesPerTimeStep;
	using DataGlobals::ZoneTSReporting;
	using DataGlobals::HVACTSReporting;
	using DataGlobals::StdOutputRecordCount;
	using DataEnvironment::Month;
	using DataEnvironment::DayOfMonth;
	using DataEnvironment::Year;
	using DataEnvironment::DSTIndicator;
	using DataEnvironment::DayOfWeek;
	using DataEnvironment::HolidayIndex;
	using namespace DataGlobalConstants;

	// Data
	// in this file should obey a USE OutputProcessor, ONLY: rule.

	// MODULE PARAMETER DEFINITIONS:
	int const ReportEach( -1 ); // Write out each time UpdatedataandReport is called
	int const ReportTimeStep( 0 ); // Write out at 'EndTimeStepFlag'
	int const ReportHourly( 1 ); // Write out at 'EndHourFlag'
	int const ReportDaily( 2 ); // Write out at 'EndDayFlag'
	int const ReportMonthly( 3 ); // Write out at end of month (must be determined)
	int const ReportSim( 4 ); // Write out once per environment 'EndEnvrnFlag'

	int const ReportVDD_No( 0 ); // Don't report the variable dictionaries in any form
	int const ReportVDD_Yes( 1 ); // Report the variable dictionaries in "report format"
	int const ReportVDD_IDF( 2 ); // Report the variable dictionaries in "IDF format"

	Real64 const MinSetValue( 99999999999999. );
	Real64 const MaxSetValue( -99999999999999. );
	int const IMinSetValue( 999999 );
	int const IMaxSetValue( -999999 );

	int const ZoneVar( 1 ); // Type value for those variables reported on the Zone Time Step
	int const HVACVar( 2 ); // Type value for those variables reported on the System Time Step

	int const AveragedVar( 1 ); // Type value for "averaged" variables
	int const SummedVar( 2 ); // Type value for "summed" variables

	int const VarType_NotFound( 0 ); // ref: GetVariableKeyCountandType, 0 = not found
	int const VarType_Integer( 1 ); // ref: GetVariableKeyCountandType, 1 = integer
	int const VarType_Real( 2 ); // ref: GetVariableKeyCountandType, 2 = real
	int const VarType_Meter( 3 ); // ref: GetVariableKeyCountandType, 3 = meter
	int const VarType_Schedule( 4 ); // ref: GetVariableKeyCountandType, 4 = schedule

	int const MeterType_Normal( 0 ); // Type value for normal meters
	int const MeterType_Custom( 1 ); // Type value for custom meters
	int const MeterType_CustomDec( 2 ); // Type value for custom meters that decrement another meter
	int const MeterType_CustomDiff( 3 ); // Type value for custom meters that difference another meter

	Fstring const TimeStampFormat( "(A,',',A,',',i2,',',i2,',',i2,',',i2,',',f5.2,',',f5.2,',',A)" );
	Fstring const DailyStampFormat( "(A,',',A,',',i2,',',i2,',',i2,',',A)" );
	Fstring const MonthlyStampFormat( "(A,',',A,',',i2)" );
	Fstring const RunPeriodStampFormat( "(A,',',A)" );
	Fstring const fmta( "(A)" );
	FArray1D_Fstring const DayTypes( 12, sFstring( 15 ), { "Sunday         ", "Monday         ", "Tuesday        ", "Wednesday      ", "Thursday       ", "Friday         ", "Saturday       ", "Holiday        ", "SummerDesignDay", "WinterDesignDay", "CustomDay1     ", "CustomDay2     " } );
	Fstring const BlankString;
	int const UnitsStringLength( 16 );

	int const RVarAllocInc( 1000 );
	int const LVarAllocInc( 1000 );
	int const IVarAllocInc( 10 );

	//  For IP Units (tabular reports) certain resources will be put in sub-tables
	//INTEGER, PARAMETER :: RT_IPUnits_Consumption=0
	int const RT_IPUnits_Electricity( 1 );
	int const RT_IPUnits_Gas( 2 );
	int const RT_IPUnits_Cooling( 3 );
	int const RT_IPUnits_Water( 4 );
	int const RT_IPUnits_OtherKG( 5 );
	int const RT_IPUnits_OtherM3( 6 );
	int const RT_IPUnits_OtherL( 7 );
	int const RT_IPUnits_OtherJ( 0 );

	// DERIVED TYPE DEFINITIONS:

	int InstMeterCacheSize( 1000 ); // the maximum size of the instant meter cache used in GetInstantMeterValue
	int InstMeterCacheSizeInc( 1000 ); // the increment for the instant meter cache used in GetInstantMeterValue
	FArray1D_int InstMeterCache; // contains a list of RVariableTypes that make up a specific meter
	FArray1D_int InstMeterCacheCopy; // for dynamic array resizing
	int InstMeterCacheLastUsed( 0 ); // the last item in the instant meter cache used

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// MODULE VARIABLE DECLARATIONS:

	int CurrentReportNumber( 0 );
	int NumVariablesForOutput( 0 );
	int MaxVariablesForOutput( 0 );
	int NumOfRVariable_Setup( 0 );
	int NumTotalRVariable( 0 );
	int NumOfRVariable_Sum( 0 );
	int NumOfRVariable_Meter( 0 );
	int NumOfRVariable( 0 );
	int MaxRVariable( 0 );
	int NumOfIVariable_Setup( 0 );
	int NumTotalIVariable( 0 );
	int NumOfIVariable_Sum( 0 );
	int NumOfIVariable( 0 );
	int MaxIVariable( 0 );
	bool OutputInitialized( false );
	int ProduceReportVDD( ReportVDD_No );
	int OutputFileRVDD( 0 ); // Unit number for Report Variables Data Dictionary (output)
	int OutputFileMVDD( 0 ); // Unit number for Meter Variables Data Dictionary (output)
	int OutputFileMeterDetails( 0 ); // Unit number for Meter Details file (output)
	int NumHoursInDay( 24 );
	int NumHoursInMonth( 0 );
	int NumHoursInSim( 0 );
	FArray1D_int ReportList;
	int NumReportList( 0 );
	int NumExtraVars( 0 );
	FArray2D_Fstring FreqNotice( {-1,4}, {1,2}, sFstring( 80 ) ); // =(/'! Each Call','! TimeStep',' !Hourly',',Daily',',Monthly',',Environment'/)

	int NumOfReqVariables( 0 ); // Current number of Requested Report Variables

	int NumVarMeterArrays( 0 ); // Current number of Arrays pointing to meters

	int NumEnergyMeters( 0 ); // Current number of Energy Meters
	FArray1D< Real64 > MeterValue; // This holds the current timestep value for each meter.

	int TimeStepStampReportNbr; // TimeStep and Hourly Report number
	Fstring TimeStepStampReportChr( 3 ); // TimeStep and Hourly Report number (character -- for printing)
	bool TrackingHourlyVariables( false ); // Requested Hourly Report Variables
	int DailyStampReportNbr; // Daily Report number
	Fstring DailyStampReportChr( 3 ); // Daily Report number (character -- for printing)
	bool TrackingDailyVariables( false ); // Requested Daily Report Variables
	int MonthlyStampReportNbr; // Monthly Report number
	Fstring MonthlyStampReportChr( 3 ); // Monthly Report number (character -- for printing)
	bool TrackingMonthlyVariables( false ); // Requested Monthly Report Variables
	int RunPeriodStampReportNbr; // RunPeriod Report number
	Fstring RunPeriodStampReportChr( 3 ); // RunPeriod Report number (character -- for printing)
	bool TrackingRunPeriodVariables( false ); // Requested RunPeriod Report Variables
	Real64 SecondsPerTimeStep; // Seconds from NumTimeStepInHour
	bool ErrorsLogged( false );
	bool ProduceVariableDictionary( false );

	int MaxNumSubcategories( 1 );

	// All routines should be listed here whether private or not
	//PUBLIC  ReallocateTVar
	//PUBLIC  SetReportNow

	// Object Data
	FArray1D< TimeSteps > TimeValue( 2 ); // Pointers to the actual TimeStep variables
	FArray1D< RealVariableType > RVariableTypes; // Variable Types structure (use NumOfRVariables to traverse)
	FArray1D< IntegerVariableType > IVariableTypes; // Variable Types structure (use NumOfIVariables to traverse)
	FArray1D< VariableTypeForDDOutput > DDVariableTypes; // Variable Types structure (use NumVariablesForOutput to traverse)
	Reference< RealVariables > RVariable;
	Reference< IntegerVariables > IVariable;
	Reference< RealVariables > RVar;
	Reference< IntegerVariables > IVar;
	FArray1D< ReqReportVariables > ReqRepVars;
	FArray1D< MeterArrayType > VarMeterArrays;
	FArray1D< MeterType > EnergyMeters;
	FArray1D< EndUseCategoryType > EndUseCategory;

	// Routines tagged on the end of this module:
	//  AddToOutputVariableList
	//  AssignReportNumber
	//  GenOutputVariablesAuditReport
	//  GetCurrentMeterValue
	//  GetInstantMeterValue
	//  GetInternalVariableValue
	//  GetInternalVariableValueExternalInterface
	//  GetMeteredVariables
	//  GetMeterIndex
	//  GetMeterResourceType
	//  GetNumMeteredVariables
	//  GetVariableKeyCountandType
	//  GetVariableKeys
	//  InitPollutionMeterReporting
	//  ProduceRDDMDD
	//  ReportingThisVariable
	//  SetInitialMeterReportingAndOutputNames
	//  SetupOutputVariable
	//  UpdateDataandReport
	//  UpdateMeterReporting

	// Functions

	void
	InitializeOutput()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine initializes the OutputProcessor data structures.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:

		RVariableTypes.allocate( RVarAllocInc );
		RVar.allocate();
		MaxRVariable = RVarAllocInc;

		IVariableTypes.allocate( IVarAllocInc );
		IVar.allocate();
		MaxIVariable = IVarAllocInc;

		// First index is the frequency designation (-1 = each call, etc)
		// Second index is the variable type (1=Average, 2=Sum)
		// Note, Meters always report like Average (with min/max, etc) for hourly and above
		FreqNotice( -1, 1 ) = " !Each Call";
		FreqNotice( 0, 1 ) = " !TimeStep";
		FreqNotice( 1, 1 ) = " !Hourly";
		FreqNotice( 2, 1 ) = " !Daily [Value,Min,Hour,Minute,Max,Hour,Minute]";
		FreqNotice( 3, 1 ) = " !Monthly [Value,Min,Day,Hour,Minute,Max,Day,Hour,Minute]";
		FreqNotice( 4, 1 ) = " !RunPeriod [Value,Min,Month,Day,Hour,Minute,Max,Month,Day,Hour,Minute]";
		FreqNotice( -1, 2 ) = " !Each Call";
		FreqNotice( 0, 2 ) = " !TimeStep";
		FreqNotice( 1, 2 ) = " !Hourly";
		FreqNotice( 2, 2 ) = " !Daily  [Value,Min,Hour,Minute,Max,Hour,Minute]";
		FreqNotice( 3, 2 ) = " !Monthly  [Value,Min,Day,Hour,Minute,Max,Day,Hour,Minute]";
		FreqNotice( 4, 2 ) = " !RunPeriod [Value,Min,Month,Day,Hour,Minute,Max,Month,Day,Hour,Minute]";

		ReportList.allocate( 500 );
		NumReportList = 500;
		ReportList = 0;
		NumExtraVars = 0;

		// Initialize end use category names - the indices must match up with endUseNames in OutputReportTabular
		EndUseCategory.allocate( NumEndUses );
		EndUseCategory( endUseHeating ).Name = "Heating";
		EndUseCategory( endUseCooling ).Name = "Cooling";
		EndUseCategory( endUseInteriorLights ).Name = "InteriorLights";
		EndUseCategory( endUseExteriorLights ).Name = "ExteriorLights";
		EndUseCategory( endUseInteriorEquipment ).Name = "InteriorEquipment";
		EndUseCategory( endUseExteriorEquipment ).Name = "ExteriorEquipment";
		EndUseCategory( endUseFans ).Name = "Fans";
		EndUseCategory( endUsePumps ).Name = "Pumps";
		EndUseCategory( endUseHeatRejection ).Name = "HeatRejection";
		EndUseCategory( endUseHumidification ).Name = "Humidifier";
		EndUseCategory( endUseHeatRecovery ).Name = "HeatRecovery";
		EndUseCategory( endUseWaterSystem ).Name = "WaterSystems";
		EndUseCategory( endUseRefrigeration ).Name = "Refrigeration";
		EndUseCategory( endUseCogeneration ).Name = "Cogeneration";

		// Initialize display names for output table - this could go away if end use key names are changed to match
		EndUseCategory( endUseHeating ).DisplayName = "Heating";
		EndUseCategory( endUseCooling ).DisplayName = "Cooling";
		EndUseCategory( endUseInteriorLights ).DisplayName = "Interior Lighting";
		EndUseCategory( endUseExteriorLights ).DisplayName = "Exterior Lighting";
		EndUseCategory( endUseInteriorEquipment ).DisplayName = "Interior Equipment";
		EndUseCategory( endUseExteriorEquipment ).DisplayName = "Exterior Equipment";
		EndUseCategory( endUseFans ).DisplayName = "Fans";
		EndUseCategory( endUsePumps ).DisplayName = "Pumps";
		EndUseCategory( endUseHeatRejection ).DisplayName = "Heat Rejection";
		EndUseCategory( endUseHumidification ).DisplayName = "Humidification";
		EndUseCategory( endUseHeatRecovery ).DisplayName = "Heat Recovery";
		EndUseCategory( endUseWaterSystem ).DisplayName = "Water Systems";
		EndUseCategory( endUseRefrigeration ).DisplayName = "Refrigeration";
		EndUseCategory( endUseCogeneration ).DisplayName = "Generators";

		OutputInitialized = true;

		SecondsPerTimeStep = double( MinutesPerTimeStep ) * 60.0;

		InitializeMeters();

	}

	void
	SetupTimePointers(
		Fstring const & IndexKey, // Which timestep is being set up, 'Zone'=1, 'HVAC'=2
		Real64 & TimeStep // The timestep variable.  Used to get the address
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine sets up the derived type for the output processor that
		// contains pointers to the TimeStep values used in the simulation.

		// METHODOLOGY EMPLOYED:
		// Indicate that the TimeStep passed in is a target for the pointer
		// attributes in the derived types.

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataGlobals::OutputFileStandard;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// for the pointer in the derived type.

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring cValue( 25 );
		int Index;

		Index = ValidateIndexType( IndexKey, "SetupTimePointers" );

		{ auto const SELECT_CASE_var( Index );

		if ( SELECT_CASE_var == 1 ) {
			TimeValue( Index ).TimeStep >>= TimeStep;
			TimeValue( Index ).CurMinute = 0.0;

		} else if ( SELECT_CASE_var == 2 ) {
			TimeValue( Index ).TimeStep >>= TimeStep;
			TimeValue( Index ).CurMinute = 0.0;

		} else {
			gio::write( cValue, "*" ) << Index;
			ShowSevereError( "Illegal value passed to SetupTimePointers, must be 1 or 2 == " + trim( cValue ), OutputFileStandard );

		}}

	}

	void
	CheckReportVariable(
		Fstring const & KeyedValue, // Associated Key for this variable
		Fstring const & VarName // String Name of variable (without units)
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine will get the report variable information from input and
		// determine if this variable (KeyedValue and VariableName) should be reported
		// and, if so, what frequency to report.

		// This routine is called when SetupOutputVariable is called with no "optional"
		// Reporting Frequency.  It is expected that SetupOutputVariable would only be
		// called once for each keyed variable to be triggered for output (from the input
		// requests).  The optional report frequency would only be used for debugging
		// purposes.  Therefore, this routine will collect all occasions where this
		// passed variablename would be reported from the requested input.  It builds
		// a list of these requests (ReportList) so that the calling routine can propagate
		// the requests into the correct data structure.

		// METHODOLOGY EMPLOYED:
		// This instance being requested will always have a key associated with it.  Matching
		// instances (from input) may or may not have keys, but only one instance of a reporting
		// frequency per variable is allowed.  ReportList will be populated with ReqRepVars indices
		// of those extra things from input that satisfy this condition.

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::FindItem;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		static bool GetInputFlag( true );
		int Item;
		int Loop;
		int Pos;
		int MinLook;
		int MaxLook;

		if ( GetInputFlag ) {
			GetReportVariableInput();
			GetInputFlag = false;
		}

		if ( NumOfReqVariables > 0 ) {
			// Do a quick check
			Item = FindItem( VarName, ReqRepVars( {1,NumOfReqVariables} ).VarName(), NumOfReqVariables );

			NumExtraVars = 0;
			ReportList = 0;
			MinLook = 999999999;
			MaxLook = -999999999;

			if ( Item != 0 ) {
				Loop = Item;
				Pos = Item;
				MinLook = min( MinLook, Pos );
				MaxLook = max( MaxLook, Pos );
				while ( Loop <= NumOfReqVariables && Pos != 0 ) {
					//  Mark all with blank keys as used
					if ( ReqRepVars( Loop ).Key == BlankString ) {
						ReqRepVars( Loop ).Used = true;
					}
					if ( Loop < NumOfReqVariables ) {
						Pos = FindItem( VarName, ReqRepVars( {Loop + 1,NumOfReqVariables} ).VarName(), NumOfReqVariables - Loop );
						if ( Pos != 0 ) {
							MinLook = min( MinLook, Loop + Pos );
							MaxLook = max( MaxLook, Loop + Pos );
						}
					} else {
						Pos = 1;
					}
					Loop += Pos;
				}
				BuildKeyVarList( KeyedValue, VarName, MinLook, MaxLook );
				AddBlankKeys( VarName, MinLook, MaxLook );
			}
		}

	}

	void
	BuildKeyVarList(
		Fstring const & KeyedValue, // Associated Key for this variable
		Fstring const & VariableName, // String Name of variable
		int const MinIndx, // Min number (from previous routine) for this variable
		int const MaxIndx // Max number (from previous routine) for this variable
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   March 1999
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine builds an initial list (from ReqRepVars) of
		// pointers to that data structure for this KeyedValue and VariableName.

		// METHODOLOGY EMPLOYED:
		// Go through the ReqRepVars list and add those
		// that match (and dont duplicate ones already in the list).

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::SameString;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop;
		int Loop1;
		bool Dup;
		FArray1D_int TmpReportList;

		for ( Loop = MinIndx; Loop <= MaxIndx; ++Loop ) {
			if ( ! SameString( ReqRepVars( Loop ).VarName, VariableName ) ) continue;
			if ( ! SameString( ReqRepVars( Loop ).Key, KeyedValue ) ) continue;

			//   A match.  Make sure doesnt duplicate

			ReqRepVars( Loop ).Used = true;
			Dup = false;
			for ( Loop1 = 1; Loop1 <= NumExtraVars; ++Loop1 ) {
				if ( ReqRepVars( ReportList( Loop1 ) ).ReportFreq == ReqRepVars( Loop ).ReportFreq ) {
					Dup = true;
				} else {
					continue;
				}
				//  So Same Report Frequency
				if ( ReqRepVars( ReportList( Loop1 ) ).SchedPtr != ReqRepVars( Loop ).SchedPtr ) Dup = false;
			}

			if ( ! Dup ) {
				++NumExtraVars;
				if ( NumExtraVars == NumReportList ) {
					TmpReportList.allocate( NumReportList );
					TmpReportList = 0;
					TmpReportList( {1,NumReportList} ) = ReportList;
					ReportList.deallocate();
					NumReportList += 100;
					ReportList.allocate( NumReportList );
					ReportList = TmpReportList;
					TmpReportList.deallocate();
				}
				ReportList( NumExtraVars ) = Loop;
			}

		}

	}

	void
	AddBlankKeys(
		Fstring const & VariableName, // String Name of variable
		int const MinIndx, // Min number (from previous routine) for this variable
		int const MaxIndx // Max number (from previous routine) for this variable
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   March 1999
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine adds to the ReportList any report variables that have
		// been requested for all keys of that report variable (if it doesnt duplicate
		// a frequency already on the list).

		// METHODOLOGY EMPLOYED:
		// Go through the ReqRepVars list and add those
		// that match (and dont duplicate ones already in the list).

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::SameString;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop;
		int Loop1;
		bool Dup;
		FArray1D_int TmpReportList;

		for ( Loop = MinIndx; Loop <= MaxIndx; ++Loop ) {
			if ( ReqRepVars( Loop ).Key != BlankString ) continue;
			if ( ! SameString( ReqRepVars( Loop ).VarName, VariableName ) ) continue;

			//   A match.  Make sure doesnt duplicate

			Dup = false;
			for ( Loop1 = 1; Loop1 <= NumExtraVars; ++Loop1 ) {
				//IF (ReqRepVars(ReportList(Loop1))%ReportFreq == ReqRepVars(Loop)%ReportFreq) Dup=.TRUE.
				if ( ReqRepVars( ReportList( Loop1 ) ).ReportFreq == ReqRepVars( Loop ).ReportFreq ) {
					Dup = true;
				} else {
					continue;
				}
				//  So Same Report Frequency
				if ( ReqRepVars( ReportList( Loop1 ) ).SchedPtr != ReqRepVars( Loop ).SchedPtr ) Dup = false;
			}

			if ( ! Dup ) {
				++NumExtraVars;
				if ( NumExtraVars == NumReportList ) {
					TmpReportList.allocate( NumReportList );
					TmpReportList = 0;
					TmpReportList( {1,NumReportList} ) = ReportList;
					ReportList.deallocate();
					NumReportList += 100;
					ReportList.allocate( NumReportList );
					ReportList = TmpReportList;
					TmpReportList.deallocate();
				}
				ReportList( NumExtraVars ) = Loop;
			}

		}

	}

	void
	GetReportVariableInput()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine gets the requested report variables from
		// the input file.
		// Report Variable,
		//        \memo each Report Variable command picks variables to be put onto the standard output file (.eso)
		//        \memo some variables may not be reported for every simulation
		//   A1 , \field Key_Value
		//        \note use '*' (without quotes) to apply this variable to all keys
		//   A2 , \field Variable_Name
		//   A3 , \field Reporting_Frequency
		//        \type choice
		//        \key detailed
		//        \key timestep
		//        \key hourly
		//        \key daily
		//        \key monthly
		//        \key runperiod
		//   A4 ; \field Schedule_Name
		//        \type object-list
		//        \object-list ScheduleNames

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		//using namespace DataIPShortCuts;
		using DataGlobals::OutputFileInits;
		using namespace InputProcessor;
		using ScheduleManager::GetScheduleIndex;
		using DataSystemVariables::cMinReportFrequency;
		using DataSystemVariables::MinReportFrequency;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop;
		int NumAlpha;
		int NumNumbers;
		int IOStat;
		int Item;
		static bool ErrorsFound( false ); // If errors detected in input
		Fstring cCurrentModuleObject( MaxNameLength );
		FArray1D_Fstring cAlphaArgs( 4, sFstring( MaxNameLength ) );
		FArray1D_Fstring cAlphaFieldNames( 4, sFstring( MaxNameLength ) );
		FArray1D_bool lAlphaFieldBlanks( 4 );
		FArray1D< Real64 > rNumericArgs( 1 );
		FArray1D_Fstring cNumericFieldNames( 1, sFstring( MaxNameLength ) );
		FArray1D_bool lNumericFieldBlanks( 1 );

		// Formats
		std::string const Format_800( "('! <Minimum Reporting Frequency (overriding input value)>, Value, Input Value')" );
		std::string const Format_801( "(' Minimum Reporting Frequency, ',A,',',A)" );

		// First check environment variable to see of possible override for minimum reporting frequency
		if ( cMinReportFrequency != " " ) {
			DetermineFrequency( cMinReportFrequency, Item ); // Use local variable Item so as not to possibly confuse things
			MinReportFrequency = max( MinReportFrequency, Item );
			gio::write( OutputFileInits, Format_800 );
			gio::write( OutputFileInits, Format_801 ) << trim( FreqNotice( MinReportFrequency, 1 ) ) << trim( cMinReportFrequency );
		}

		cCurrentModuleObject = "Output:Variable";
		NumOfReqVariables = GetNumObjectsFound( cCurrentModuleObject );
		ReqRepVars.allocate( NumOfReqVariables );

		for ( Loop = 1; Loop <= NumOfReqVariables; ++Loop ) {

			GetObjectItem( cCurrentModuleObject, Loop, cAlphaArgs, NumAlpha, rNumericArgs, NumNumbers, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );

			// Check for duplicates?

			ReqRepVars( Loop ).Key = cAlphaArgs( 1 );
			if ( ReqRepVars( Loop ).Key == "*" ) {
				ReqRepVars( Loop ).Key = BlankString;
			}

			Item = index( cAlphaArgs( 2 ), "[" ); // Remove Units designation if user put it in
			if ( Item != 0 ) {
				cAlphaArgs( 2 ) = cAlphaArgs( 2 )( 1, Item - 1 );
			}
			ReqRepVars( Loop ).VarName = cAlphaArgs( 2 );

			DetermineFrequency( cAlphaArgs( 3 ), ReqRepVars( Loop ).ReportFreq );

			// Schedule information
			ReqRepVars( Loop ).SchedName = cAlphaArgs( 4 );
			if ( ReqRepVars( Loop ).SchedName != "   " ) {
				ReqRepVars( Loop ).SchedPtr = GetScheduleIndex( ReqRepVars( Loop ).SchedName );
				if ( ReqRepVars( Loop ).SchedPtr == 0 ) {
					ShowSevereError( "GetReportVariableInput: " + trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + ":" + trim( ReqRepVars( Loop ).VarName ) + "\" invalid " + trim( cAlphaFieldNames( 4 ) ) + "=\"" + trim( ReqRepVars( Loop ).SchedName ) + "\" - not found." );
					ErrorsFound = true;
				}
			} else {
				ReqRepVars( Loop ).SchedPtr = 0;
			}

			ReqRepVars( Loop ).Used = false;

		}

		if ( ErrorsFound ) {
			ShowFatalError( "GetReportVariableInput:" + trim( cCurrentModuleObject ) + ": errors in input." );
		}

	}

	void
	DetermineFrequency(
		Fstring const & FreqString,
		int & ReportFreq
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine looks at the passed in report frequency string and
		// determines the reporting frequency.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		//       \field Reporting Frequency
		//       \type choice
		//       \key Detailed
		//       \note Detailed lists every instance (i.e. HVAC variable timesteps)
		//       \key Timestep
		//       \note Timestep refers to the zone Timestep/Number of Timesteps in hour value
		//       \note RunPeriod, Environment, and Annual are the same
		//       \key Hourly
		//       \key Daily
		//       \key Monthly
		//       \key RunPeriod
		//       \key Environment
		//       \key Annual
		//       \default Hourly
		//       \note RunPeriod, Environment, and Annual are synonymous

		// Using/Aliasing
		using InputProcessor::SameString;
		using DataSystemVariables::MinReportFrequency;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		static FArray1D_Fstring const PossibleFreq( {-1,6}, sFstring( 4 ), { "deta", "time", "hour", "dail", "mont", "runp", "envi", "annu" } );
		//=(/'detail','Timestep','Hourly','Daily','Monthly','RunPeriod','Environment','Annual'/)
		static FArray1D_Fstring const ExactFreqString( {-1,6}, sFstring( 11 ), { "Detailed   ", "Timestep   ", "Hourly     ", "Daily      ", "Monthly    ", "RunPeriod  ", "Environment", "Annual     " } );

		static FArray1D_int const FreqValues( {-1,6}, { -1, 0, 1, 2, 3, 4, 4, 4 } );
		// note: runperiod, environment, and annual are synonomous

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop;
		int LenString;

		ReportFreq = ReportHourly; //Default
		LenString = len( FreqString );
		LenString = min( LenString, 4 );

		for ( Loop = -1; Loop <= 6; ++Loop ) {
			if ( ! SameString( FreqString( 1, LenString ), PossibleFreq( Loop )( 1, 4 ) ) ) continue;
			if ( ! SameString( FreqString, ExactFreqString( Loop ) ) ) {
				ShowWarningError( "DetermineFrequency: Entered frequency=\"" + trim( FreqString ) + "\" is not an exact match to key strings." );
				ShowContinueError( "Frequency=" + trim( ExactFreqString( Loop ) ) + " will be used." );
			}
			ReportFreq = max( FreqValues( Loop ), MinReportFrequency );
			break;
		}

	}

	void
	ProduceMinMaxString(
		Fstring & String, // Current value
		int const DateValue, // Date of min/max
		int const ReportFreq // Reporting Frequency
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine produces the appropriate min/max string depending
		// on the reporting frequency.

		// METHODOLOGY EMPLOYED:
		// Prior to calling this routine, the basic value string will be
		// produced, but DecodeMonDayHrMin will not have been called.

		// REFERENCES:
		// na

		// Using/Aliasing
		using General::DecodeMonDayHrMin;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		static Fstring const DayFormat( "(A,',',I2,',',I2)" );
		static Fstring const MonthFormat( "(A,',',I2,',',I2,',',I2)" );
		static Fstring const EnvrnFormat( "(A,',',I2,',',I2,',',I2,',',I2)" );

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Mon;
		int Day;
		int Hour;
		int Minute;
		Fstring TempString( 40 );
		Fstring StrOut( 40 );

		TempString = adjustl( String );
		DecodeMonDayHrMin( DateValue, Mon, Day, Hour, Minute );

		{ auto const SELECT_CASE_var( ReportFreq );

		if ( SELECT_CASE_var == 2 ) { // Daily
			gio::write( StrOut, DayFormat ) << trim( TempString ) << Hour << Minute;

		} else if ( SELECT_CASE_var == 3 ) { // Monthly
			gio::write( StrOut, MonthFormat ) << trim( TempString ) << Day << Hour << Minute;

		} else if ( SELECT_CASE_var == 4 ) { // Environment
			gio::write( StrOut, EnvrnFormat ) << trim( TempString ) << Mon << Day << Hour << Minute;

		} else { // Each, TimeStep, Hourly dont have this
			StrOut = BlankString;

		}}

		String = StrOut;

	}

	void
	ProduceMinMaxStringWStartMinute(
		Fstring & String, // Current value
		int const DateValue, // Date of min/max
		int const ReportFreq // Reporting Frequency
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine produces the appropriate min/max string depending
		// on the reporting frequency.  Used in Meter reporting.

		// METHODOLOGY EMPLOYED:
		// Prior to calling this routine, the basic value string will be
		// produced, but DecodeMonDayHrMin will not have been called.  Uses the MinutesPerTimeStep
		// value to set the StartMinute.

		// REFERENCES:
		// na

		// Using/Aliasing
		using General::DecodeMonDayHrMin;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		static Fstring const HrFormat( "(A,',',I2.2,':',I2.2)" );
		static Fstring const DayFormat( "(A,',',I2,',',I2.2,':',I2.2)" );
		static Fstring const MonthFormat( "(A,',',I2,',',I2,',',I2.2,':',I2.2)" );
		static Fstring const EnvrnFormat( "(A,',',I2,',',I2,',',I2,',',I2.2,':',I2.2)" );

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Mon;
		int Day;
		int Hour;
		int Minute;
		int StartMinute;
		Fstring TempString( 40 );
		Fstring StrOut( 40 );

		TempString = adjustl( String );
		DecodeMonDayHrMin( DateValue, Mon, Day, Hour, Minute );

		{ auto const SELECT_CASE_var( ReportFreq );

		if ( SELECT_CASE_var == 1 ) { // Hourly -- used in meters
			StartMinute = Minute - MinutesPerTimeStep + 1;
			gio::write( StrOut, HrFormat ) << trim( TempString ) << StartMinute << Minute;

		} else if ( SELECT_CASE_var == 2 ) { // Daily
			StartMinute = Minute - MinutesPerTimeStep + 1;
			gio::write( StrOut, DayFormat ) << trim( TempString ) << Hour << StartMinute << Minute;

		} else if ( SELECT_CASE_var == 3 ) { // Monthly
			StartMinute = Minute - MinutesPerTimeStep + 1;
			gio::write( StrOut, MonthFormat ) << trim( TempString ) << Day << Hour << StartMinute << Minute;

		} else if ( SELECT_CASE_var == 4 ) { // Environment
			StartMinute = Minute - MinutesPerTimeStep + 1;
			gio::write( StrOut, EnvrnFormat ) << trim( TempString ) << Mon << Day << Hour << StartMinute << Minute;

		} else { // Each, TimeStep, Hourly dont have this
			StrOut = BlankString;

		}}

		String = StrOut;

	}

	void
	ReallocateIntegerArray(
		FArray1D_int & Array,
		int & ArrayMax, // Current and resultant dimension for Array
		int const ArrayInc // increment for redimension
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   March 2008
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine reallocates (preserving data) an Integer array
		// for the output processor.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Argument array dimensioning

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		FArray1D_int NewArray;

		NewArray.allocate( ArrayMax + ArrayInc );
		NewArray = 0;

		if ( ArrayMax > 0 ) {
			NewArray( {1,ArrayMax} ) = Array( {1,ArrayMax} );
		}
		Array.deallocate();
		ArrayMax += ArrayInc;
		Array.allocate( ArrayMax );
		Array = NewArray;
		NewArray.deallocate();

	}

	void
	ReallocateRVar()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine reallocates (preserving data) the Real Variable
		// structure for the output processor.

		// METHODOLOGY EMPLOYED:
		// Using the current value of MaxRVariable, this routine allocates
		// the new dimension for the arrays xxx, xxxx, and RealVariables structure.

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:

		// Object Data
		FArray1D< RealVariableType > Types;

		Types.allocate( MaxRVariable + RVarAllocInc );

		if ( MaxRVariable > 0 ) {
			Types( {1,MaxRVariable} ) = RVariableTypes( {1,MaxRVariable} );
		}
		RVariableTypes.deallocate();
		MaxRVariable += RVarAllocInc;
		RVariableTypes.allocate( MaxRVariable );
		RVariableTypes = Types;
		Types.deallocate();

	}

	void
	ReallocateIVar()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine performs a "reallocate" on the Integer Report
		// Variable structure.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:

		// Object Data
		FArray1D< IntegerVariableType > Types;

		Types.allocate( MaxIVariable + IVarAllocInc );

		if ( MaxIVariable > 0 ) {
			Types( {1,MaxIVariable} ) = IVariableTypes( {1,MaxIVariable} );
		}
		IVariableTypes.deallocate();
		MaxIVariable += IVarAllocInc;
		IVariableTypes.allocate( MaxIVariable );
		IVariableTypes = Types;
		Types.deallocate();

	}

	int
	ValidateIndexType(
		Fstring const & IndexTypeKey, // Index type (Zone, HVAC) for variables
		Fstring const & CalledFrom // Routine called from (for error messages)
	)
	{

		// FUNCTION INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS FUNCTION:
		// This function validates the requested "index" type and returns
		// the proper value for use inside the OutputProcessor.

		// METHODOLOGY EMPLOYED:
		// Look it up in a list of valid index types.

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::FindItemInList;
		using InputProcessor::MakeUPPERCase;

		// Return value
		int ValidateIndexType;

		// Locals
		// FUNCTION ARGUMENT DEFINITIONS:

		// FUNCTION PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// FUNCTION LOCAL VARIABLE DECLARATIONS:
		static FArray1D_Fstring ZoneIndexTypes( 3, sFstring( 12 ) );
		static FArray1D_Fstring SystemIndexTypes( 3, sFstring( 12 ) );
		static bool Initialized( false );
		int Item;

		if ( ! Initialized ) {
			ZoneIndexTypes( 1 ) = "ZONE";
			ZoneIndexTypes( 2 ) = "HEATBALANCE";
			ZoneIndexTypes( 3 ) = "HEAT BALANCE";
			SystemIndexTypes( 1 ) = "HVAC";
			SystemIndexTypes( 2 ) = "SYSTEM";
			SystemIndexTypes( 3 ) = "PLANT";
			Initialized = true;
		}

		ValidateIndexType = 1;
		Item = FindItemInList( MakeUPPERCase( IndexTypeKey ), ZoneIndexTypes, 3 );
		if ( Item != 0 ) return ValidateIndexType;

		ValidateIndexType = 2;
		Item = FindItemInList( MakeUPPERCase( IndexTypeKey ), SystemIndexTypes, 3 );
		if ( Item != 0 ) return ValidateIndexType;

		ValidateIndexType = 0;
		//  The following should never happen to a user!!!!
		ShowSevereError( "OutputProcessor/ValidateIndexType: Invalid Index Key passed to ValidateIndexType=" + trim( IndexTypeKey ) );
		ShowContinueError( "..Should be \"ZONE\", \"SYSTEM\", \"HVAC\"... was called from:" + trim( CalledFrom ) );
		ShowFatalError( "Preceding condition causes termination." );

		return ValidateIndexType;

	}

	Fstring
	StandardIndexTypeKey( int const IndexType )
	{

		// FUNCTION INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS FUNCTION:
		// This function gives the standard string for the index type
		// given.

		// METHODOLOGY EMPLOYED:
		// Look it up in a list of valid index types.

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Return value
		Fstring StandardIndexTypeKey( 4 );

		// Locals
		// FUNCTION ARGUMENT DEFINITIONS:

		// FUNCTION PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// FUNCTION LOCAL VARIABLE DECLARATIONS:
		// na

		{ auto const SELECT_CASE_var( IndexType );

		if ( SELECT_CASE_var == 1 ) {
			StandardIndexTypeKey = "Zone";

		} else if ( SELECT_CASE_var == 2 ) {
			StandardIndexTypeKey = "HVAC";

		} else {
			StandardIndexTypeKey = "UNKW";

		}}

		return StandardIndexTypeKey;

	}

	int
	ValidateVariableType( Fstring const & VariableTypeKey )
	{

		// FUNCTION INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   December 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS FUNCTION:
		// This function validates the VariableTypeKey passed to the SetupVariable
		// routine and assigns it the value used in the OutputProcessor.

		// METHODOLOGY EMPLOYED:
		// Look it up in a list of valid variable types.

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::FindItemInList;
		using InputProcessor::MakeUPPERCase;

		// Return value
		int ValidateVariableType;

		// Locals
		// FUNCTION ARGUMENT DEFINITIONS:

		// FUNCTION PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// FUNCTION LOCAL VARIABLE DECLARATIONS:
		FArray1D_Fstring StateVariables( 3, sFstring( 8 ) );
		FArray1D_Fstring NonStateVariables( 4, sFstring( 9 ) );
		static bool Initialized( false );
		int Item;

		if ( ! Initialized ) {
			StateVariables( 1 ) = "STATE";
			StateVariables( 2 ) = "AVERAGE";
			StateVariables( 3 ) = "AVERAGED";
			NonStateVariables( 1 ) = "NON STATE";
			NonStateVariables( 2 ) = "NONSTATE";
			NonStateVariables( 3 ) = "SUM";
			NonStateVariables( 4 ) = "SUMMED";
		}

		ValidateVariableType = 1;
		Item = FindItemInList( MakeUPPERCase( VariableTypeKey ), StateVariables, 3 );
		if ( Item != 0 ) return ValidateVariableType;

		ValidateVariableType = 2;
		Item = FindItemInList( MakeUPPERCase( VariableTypeKey ), NonStateVariables, 4 );
		if ( Item != 0 ) return ValidateVariableType;

		ShowSevereError( "Invalid variable type requested=" + VariableTypeKey );
		ValidateVariableType = 0;

		return ValidateVariableType;

	}

	Fstring
	StandardVariableTypeKey( int const VariableType )
	{

		// FUNCTION INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   July 1999
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS FUNCTION:
		// This function gives the standard string for the variable type
		// given.

		// METHODOLOGY EMPLOYED:
		// From variable type value, produce proper string.

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Return value
		Fstring StandardVariableTypeKey( 9 );

		// Locals
		// FUNCTION ARGUMENT DEFINITIONS:

		// FUNCTION PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// FUNCTION LOCAL VARIABLE DECLARATIONS:
		// na

		{ auto const SELECT_CASE_var( VariableType );

		if ( SELECT_CASE_var == 1 ) {
			StandardVariableTypeKey = "Average";

		} else if ( SELECT_CASE_var == 2 ) {
			StandardVariableTypeKey = "Sum";

		} else {
			StandardVariableTypeKey = "Unknown";

		}}

		return StandardVariableTypeKey;

	}

	Fstring
	GetVariableUnitsString( Fstring const & VariableName )
	{

		// FUNCTION INFORMATION:
		//       AUTHOR         Linda K. Lawrie
		//       DATE WRITTEN   October 2003
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS FUNCTION:
		// This function extracts the units from a Variable Name string supplied by
		// the developer in the call to SetupOutputVariable(s).

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using General::TrimSigDigits;

		// Return value
		Fstring ThisUnitsString( UnitsStringLength );

		// Locals
		// FUNCTION ARGUMENT DEFINITIONS:

		// FUNCTION PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// FUNCTION LOCAL VARIABLE DECLARATIONS:
		int lbpos; // position of the left bracket
		int rbpos; // position of the right bracket

		// Units are marked with a [

		lbpos = index( VariableName, "[", true ); // from end of variable name

		//!! Errors here are fatal because should only be encountered during development.
		ThisUnitsString = BlankString;
		if ( lbpos > 0 ) {
			rbpos = index( VariableName, "]", true );
			if ( rbpos == 0 || rbpos < lbpos ) {
				ShowFatalError( "Ill formed Variable Name Units String, VariableName=" + trim( VariableName ) );
				ThisUnitsString = VariableName( lbpos );
			} else {
				if ( ( rbpos - 1 ) - ( lbpos + 1 ) + 1 > UnitsStringLength ) {
					ShowFatalError( "Units String too long for VariableName=" + trim( VariableName ) + "; will be truncated to " + TrimSigDigits( UnitsStringLength ) + " characters." );
				}
				if ( lbpos + 1 <= rbpos - 1 ) {
					ThisUnitsString = VariableName( lbpos + 1, rbpos - 1 );
				} else {
					ThisUnitsString = BlankString;
				}
			}
		}

		return ThisUnitsString;

	}

	// *****************************************************************************
	// The following routines implement Energy Meters in EnergyPlus.
	// *****************************************************************************

	void
	InitializeMeters()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine creates the set of meters in EnergyPlus.  In this initial
		// implementation, it is a static set of meters.

		// METHODOLOGY EMPLOYED:
		// Allocate the static set.  Use "AddMeter" with appropriate arguments that will
		// allow expansion later.

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int write_stat;

		OutputFileMeterDetails = GetNewUnitNumber();
		{ IOFlags flags; flags.ACTION( "write" ); gio::open( OutputFileMeterDetails, "eplusout.mtd", flags ); write_stat = flags.ios(); }
		if ( write_stat != 0 ) {
			ShowFatalError( "InitializeMeters: Could not open file \"eplusout.mtd\" for output (write)." );
		}

	}

	void
	GetCustomMeterInput( bool & ErrorsFound )
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2006
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This routine will help implement "custom"/user defined meters.  However, it must be called after all
		// the other meters are set up and all report variables are established.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// Processes the objects:
		// Meter:Custom,
		//    \extensible:2 - repeat last two fields, remembering to remove ; from "inner" fields.
		//    \memo Used to allow users to combine specific variables and/or meters into
		//    \memo "custom" meter configurations.
		//    A1,  \field Name
		//         \required-field
		//         \reference CustomMeterNames
		//    A2,  \field Fuel Type
		//         \type choice
		//         \key Electricity
		//         \key NaturalGas
		//         \key PropaneGas
		//         \key FuelOil#1
		//         \key FuelOil#2
		//         \key Coal
		//         \key Diesel
		//         \key Gasoline
		//         \key Water
		//         \key Generic
		//         \key OtherFuel1
		//         \key OtherFuel2
		//    A3,  \field Key Name 1
		//         \required-field
		//         \begin-extensible
		//    A4,  \field Report Variable or Meter Name 1
		//         \required-field
		// <etc>
		// AND
		// Meter:CustomDecrement,
		//    \extensible:2 - repeat last two fields, remembering to remove ; from "inner" fields.
		//    \memo Used to allow users to combine specific variables and/or meters into
		//    \memo "custom" meter configurations.
		//    A1,  \field Name
		//         \required-field
		//         \reference CustomMeterNames
		//    A2,  \field Fuel Type
		//         \type choice
		//         \key Electricity
		//         \key NaturalGas
		//         \key PropaneGas
		//         \key FuelOil#1
		//         \key FuelOil#2
		//         \key Coal
		//         \key Diesel
		//         \key Gasoline
		//         \key Water
		//         \key Generic
		//         \key OtherFuel1
		//         \key OtherFuel2
		//    A3,  \field Source Meter Name
		//         \required-field
		//    A4,  \field Key Name 1
		//         \required-field
		//         \begin-extensible
		//    A5,  \field Report Variable or Meter Name 1
		//         \required-field
		// <etc>

		// Using/Aliasing
		using namespace DataIPShortCuts;
		using namespace InputProcessor;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int NumAlpha;
		int NumNumbers;
		int Loop;
		int IOStat;
		int NumCustomMeters;
		int NumCustomDecMeters;
		bool IsNotOK;
		bool IsBlank;
		int fldIndex;
		bool KeyIsStar;
		FArray1D_Fstring NamesOfKeys( sFstring( MaxNameLength ) ); // Specific key name
		FArray1D_int IndexesForKeyVar; // Array index
		Fstring UnitsVar( MaxNameLength ); // Units sting, may be blank
		Fstring MeterUnits( MaxNameLength ); // Units sting, may be blank
		int KeyCount;
		int TypeVar;
		int AvgSumVar;
		int StepTypeVar;
		int iKey;
		int iKey1;
		bool MeterCreated;
		FArray1D_int VarsOnCustomMeter;
		FArray1D_int TempVarsOnCustomMeter;
		int MaxVarsOnCustomMeter;
		int NumVarsOnCustomMeter;
		FArray1D_int VarsOnSourceMeter;
		FArray1D_int TempVarsOnSourceMeter;
		int MaxVarsOnSourceMeter;
		int NumVarsOnSourceMeter;
		int iOnMeter;
		int WhichMeter;
		bool errFlag;
		bool BigErrorsFound;
		bool testa;
		bool testb;
		bool Tagged; // variable is appropriate to put on meter
		int lbrackPos;

		BigErrorsFound = false;

		cCurrentModuleObject = "Meter:Custom";
		NumCustomMeters = GetNumObjectsFound( cCurrentModuleObject );

		for ( Loop = 1; Loop <= NumCustomMeters; ++Loop ) {
			GetObjectItem( cCurrentModuleObject, Loop, cAlphaArgs, NumAlpha, rNumericArgs, NumNumbers, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );
			lbrackPos = index( cAlphaArgs( 1 ), "[" );
			if ( lbrackPos != 0 ) cAlphaArgs( 1 ) = cAlphaArgs( 1 )( {1,lbrackPos - 1} );
			MeterCreated = false;
			IsNotOK = false;
			IsBlank = false;
			VerifyName( cAlphaArgs( 1 ), EnergyMeters.Name(), NumEnergyMeters, IsNotOK, IsBlank, "Meter Names" );
			if ( IsNotOK ) {
				ErrorsFound = true;
				continue;
			}
			if ( allocated( VarsOnCustomMeter ) ) VarsOnCustomMeter.deallocate();
			VarsOnCustomMeter.allocate( 1000 );
			VarsOnCustomMeter = 0;
			MaxVarsOnCustomMeter = 1000;
			NumVarsOnCustomMeter = 0;
			for ( fldIndex = 3; fldIndex <= NumAlpha; fldIndex += 2 ) {
				if ( cAlphaArgs( fldIndex ) == "*" || lAlphaFieldBlanks( fldIndex ) ) {
					KeyIsStar = true;
					cAlphaArgs( fldIndex ) = "*";
				} else {
					KeyIsStar = false;
				}
				if ( lAlphaFieldBlanks( fldIndex + 1 ) ) {
					ShowSevereError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", blank " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "." );
					ShowContinueError( "...cannot create custom meter." );
					BigErrorsFound = true;
					continue;
				}
				if ( BigErrorsFound ) continue;
				// Don't build/check things out if there were errors anywhere.  Use "GetVariableKeys" to map to actual variables...
				lbrackPos = index( cAlphaArgs( fldIndex + 1 ), "[" );
				if ( lbrackPos != 0 ) cAlphaArgs( fldIndex + 1 ) = cAlphaArgs( fldIndex + 1 )( {1,lbrackPos - 1} );
				Tagged = false;
				GetVariableKeyCountandType( cAlphaArgs( fldIndex + 1 ), KeyCount, TypeVar, AvgSumVar, StepTypeVar, UnitsVar );
				if ( TypeVar == VarType_NotFound ) {
					ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", invalid " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "=\"" + trim( cAlphaArgs( fldIndex + 1 ) ) + "\"." );
					ShowContinueError( "...will not be shown with the Meter results." );
					continue;
				}
				if ( ! MeterCreated ) {
					MeterUnits = UnitsVar; // meter units are same as first variable on custom meter
					AddMeter( cAlphaArgs( 1 ), UnitsVar, BlankString, BlankString, BlankString, BlankString );
					EnergyMeters( NumEnergyMeters ).TypeOfMeter = MeterType_Custom;
					// Can't use resource type in AddMeter cause it will confuse it with other meters.  So, now:
					GetStandardMeterResourceType( EnergyMeters( NumEnergyMeters ).ResourceType, MakeUPPERCase( cAlphaArgs( 2 ) ), errFlag );
					if ( errFlag ) {
						ShowContinueError( "..on " + trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\"." );
						BigErrorsFound = true;
					}
					DetermineMeterIPUnits( EnergyMeters( NumEnergyMeters ).RT_forIPUnits, EnergyMeters( NumEnergyMeters ).ResourceType, UnitsVar, errFlag );
					if ( errFlag ) {
						ShowContinueError( "..on " + trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\"." );
						ShowContinueError( "..requests for IP units from this meter will be ignored." );
					}
					//        EnergyMeters(NumEnergyMeters)%RT_forIPUnits=DetermineMeterIPUnits(EnergyMeters(NumEnergyMeters)%ResourceType,UnitsVar)
					MeterCreated = true;
				}
				if ( UnitsVar != MeterUnits ) {
					ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", differing units in " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "=\"" + trim( cAlphaArgs( fldIndex + 1 ) ) + "\"." );
					ShowContinueError( "...will not be shown with the Meter results; units for meter=" + trim( MeterUnits ) + ", units for this variable=" + trim( UnitsVar ) + "." );
					continue;
				}
				if ( ( TypeVar == VarType_Real || TypeVar == VarType_Integer ) && AvgSumVar == SummedVar ) {
					Tagged = true;
					NamesOfKeys.allocate( KeyCount );
					IndexesForKeyVar.allocate( KeyCount );
					GetVariableKeys( cAlphaArgs( fldIndex + 1 ), TypeVar, NamesOfKeys, IndexesForKeyVar );
					iOnMeter = 0;
					if ( KeyIsStar ) {
						for ( iKey = 1; iKey <= KeyCount; ++iKey ) {
							++NumVarsOnCustomMeter;
							if ( NumVarsOnCustomMeter > MaxVarsOnCustomMeter ) {
								MaxVarsOnCustomMeter += 100;
								TempVarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
								TempVarsOnCustomMeter( {1,MaxVarsOnCustomMeter - 100} ) = VarsOnCustomMeter;
								TempVarsOnCustomMeter( {MaxVarsOnCustomMeter - 100 + 1,MaxVarsOnCustomMeter} ) = 0;
								VarsOnCustomMeter.deallocate();
								VarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
								VarsOnCustomMeter = TempVarsOnCustomMeter;
								TempVarsOnCustomMeter.deallocate();
							}
							VarsOnCustomMeter( NumVarsOnCustomMeter ) = IndexesForKeyVar( iKey );
							iOnMeter = 1;
						}
						if ( iOnMeter == 0 ) {
							ShowSevereError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", invalid (all keys) " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "=\"" + trim( cAlphaArgs( fldIndex + 1 ) ) + "\"." );
							ErrorsFound = true;
						}
					} else { // Key is not "*"
						for ( iKey = 1; iKey <= KeyCount; ++iKey ) {
							if ( NamesOfKeys( iKey ) != cAlphaArgs( fldIndex ) ) continue;
							++NumVarsOnCustomMeter;
							if ( NumVarsOnCustomMeter > MaxVarsOnCustomMeter ) {
								MaxVarsOnCustomMeter += 100;
								TempVarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
								TempVarsOnCustomMeter( {1,MaxVarsOnCustomMeter - 100} ) = VarsOnCustomMeter;
								TempVarsOnCustomMeter( {MaxVarsOnCustomMeter - 100 + 1,MaxVarsOnCustomMeter} ) = 0;
								VarsOnCustomMeter.deallocate();
								VarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
								VarsOnCustomMeter = TempVarsOnCustomMeter;
								TempVarsOnCustomMeter.deallocate();
							}
							VarsOnCustomMeter( NumVarsOnCustomMeter ) = IndexesForKeyVar( iKey );
							iOnMeter = 1;
						}
						if ( iOnMeter == 0 ) {
							ShowSevereError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", invalid " + trim( cAlphaArgs( fldIndex ) ) + ":" + trim( cAlphaArgs( fldIndex + 1 ) ) );
							ErrorsFound = true;
						}
					}
					NamesOfKeys.deallocate();
					IndexesForKeyVar.deallocate();
				}
				if ( TypeVar == VarType_Meter && AvgSumVar == SummedVar ) {
					Tagged = true;
					NamesOfKeys.allocate( KeyCount );
					IndexesForKeyVar.allocate( KeyCount );
					GetVariableKeys( cAlphaArgs( fldIndex + 1 ), TypeVar, NamesOfKeys, IndexesForKeyVar );
					WhichMeter = IndexesForKeyVar( 1 );
					NamesOfKeys.deallocate();
					IndexesForKeyVar.deallocate();
					// for meters there will only be one key...  but it has variables associated...
					for ( iOnMeter = 1; iOnMeter <= NumVarMeterArrays; ++iOnMeter ) {
						if ( ! any_eq( VarMeterArrays( iOnMeter ).OnMeters, WhichMeter ) ) continue;
						++NumVarsOnCustomMeter;
						if ( NumVarsOnCustomMeter > MaxVarsOnCustomMeter ) {
							MaxVarsOnCustomMeter += 100;
							TempVarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
							TempVarsOnCustomMeter( {1,MaxVarsOnCustomMeter - 100} ) = VarsOnCustomMeter;
							TempVarsOnCustomMeter( {MaxVarsOnCustomMeter - 100 + 1,MaxVarsOnCustomMeter} ) = 0;
							VarsOnCustomMeter.deallocate();
							VarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
							VarsOnCustomMeter = TempVarsOnCustomMeter;
							TempVarsOnCustomMeter.deallocate();
						}
						VarsOnCustomMeter( NumVarsOnCustomMeter ) = VarMeterArrays( iOnMeter ).RepVariable;
					}
				}
				if ( ! Tagged ) { // couldn't find place for this item on a meter
					if ( AvgSumVar != SummedVar ) {
						ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", variable not summed variable " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "=\"" + trim( cAlphaArgs( fldIndex + 1 ) ) + "\"." );
						ShowContinueError( "...will not be shown with the Meter results; units for meter=" + trim( MeterUnits ) + ", units for this variable=" + trim( UnitsVar ) + "." );
					}
				}
			}
			// Check for duplicates
			for ( iKey = 1; iKey <= NumVarsOnCustomMeter; ++iKey ) {
				if ( VarsOnCustomMeter( iKey ) == 0 ) continue;
				for ( iKey1 = iKey + 1; iKey1 <= NumVarsOnCustomMeter; ++iKey1 ) {
					if ( iKey == iKey1 ) continue;
					if ( VarsOnCustomMeter( iKey ) != VarsOnCustomMeter( iKey1 ) ) continue;
					ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", duplicate name=\"" + trim( RVariableTypes( VarsOnCustomMeter( iKey1 ) ).VarName ) + "\"." );
					ShowContinueError( "...only one value with this name will be shown with the Meter results." );
					VarsOnCustomMeter( iKey1 ) = 0;
				}
			}
			for ( iKey = 1; iKey <= NumVarsOnCustomMeter; ++iKey ) {
				if ( VarsOnCustomMeter( iKey ) == 0 ) continue;
				RVariable >>= RVariableTypes( VarsOnCustomMeter( iKey ) ).VarPtr;
				AttachCustomMeters( MeterUnits, VarsOnCustomMeter( iKey ), RVariable().MeterArrayPtr, NumEnergyMeters, ErrorsFound );
			}
			if ( NumVarsOnCustomMeter == 0 ) {
				ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", no items assigned " );
				ShowContinueError( "...will not be shown with the Meter results" );
			}

		}

		cCurrentModuleObject = "Meter:CustomDecrement";
		NumCustomDecMeters = GetNumObjectsFound( cCurrentModuleObject );

		for ( Loop = 1; Loop <= NumCustomDecMeters; ++Loop ) {
			GetObjectItem( cCurrentModuleObject, Loop, cAlphaArgs, NumAlpha, rNumericArgs, NumNumbers, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );
			lbrackPos = index( cAlphaArgs( 1 ), "[" );
			if ( lbrackPos != 0 ) cAlphaArgs( 1 ) = cAlphaArgs( 1 )( {1,lbrackPos - 1} );
			MeterCreated = false;
			IsNotOK = false;
			IsBlank = false;
			VerifyName( cAlphaArgs( 1 ), EnergyMeters.Name(), NumEnergyMeters, IsNotOK, IsBlank, "Meter Names" );
			if ( IsNotOK ) {
				ErrorsFound = true;
				continue;
			}
			if ( allocated( VarsOnCustomMeter ) ) VarsOnCustomMeter.deallocate();
			VarsOnCustomMeter.allocate( 1000 );
			VarsOnCustomMeter = 0;
			MaxVarsOnCustomMeter = 1000;
			NumVarsOnCustomMeter = 0;

			lbrackPos = index( cAlphaArgs( 3 ), "[" );
			if ( lbrackPos != 0 ) cAlphaArgs( 1 ) = cAlphaArgs( 3 )( {1,lbrackPos - 1} );
			WhichMeter = FindItem( cAlphaArgs( 3 ), EnergyMeters.Name(), NumEnergyMeters );
			if ( WhichMeter == 0 ) {
				ShowSevereError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", invalid " + trim( cAlphaFieldNames( 3 ) ) + "=\"" + trim( cAlphaArgs( 3 ) ) + "\"." );
				ErrorsFound = true;
				continue;
			}
			//  Set up array of Vars that are on the source meter (for later validation).
			if ( allocated( VarsOnSourceMeter ) ) VarsOnSourceMeter.deallocate();
			VarsOnSourceMeter.allocate( 1000 );
			VarsOnSourceMeter = 0;
			MaxVarsOnSourceMeter = 1000;
			NumVarsOnSourceMeter = 0;
			for ( iKey = 1; iKey <= NumVarMeterArrays; ++iKey ) {
				if ( VarMeterArrays( iKey ).NumOnMeters == 0 && VarMeterArrays( iKey ).NumOnCustomMeters == 0 ) continue;
				//  On a meter
				if ( any_eq( VarMeterArrays( iKey ).OnMeters, WhichMeter ) ) {
					++NumVarsOnSourceMeter;
					if ( NumVarsOnSourceMeter > MaxVarsOnSourceMeter ) {
						MaxVarsOnSourceMeter += 100;
						TempVarsOnSourceMeter.allocate( MaxVarsOnSourceMeter );
						TempVarsOnSourceMeter( {1,MaxVarsOnSourceMeter - 100} ) = VarsOnSourceMeter;
						TempVarsOnSourceMeter( {MaxVarsOnSourceMeter - 100 + 1,MaxVarsOnSourceMeter} ) = 0;
						VarsOnSourceMeter.deallocate();
						VarsOnSourceMeter.allocate( MaxVarsOnSourceMeter );
						VarsOnSourceMeter = TempVarsOnSourceMeter;
						TempVarsOnSourceMeter.deallocate();
					}
					VarsOnSourceMeter( NumVarsOnSourceMeter ) = VarMeterArrays( iKey ).RepVariable;
					continue;
				}
				if ( VarMeterArrays( iKey ).NumOnCustomMeters == 0 ) continue;
				if ( any_eq( VarMeterArrays( iKey ).OnCustomMeters, WhichMeter ) ) {
					++NumVarsOnSourceMeter;
					if ( NumVarsOnSourceMeter > MaxVarsOnSourceMeter ) {
						MaxVarsOnSourceMeter += 100;
						TempVarsOnSourceMeter.allocate( MaxVarsOnSourceMeter );
						TempVarsOnSourceMeter( {1,MaxVarsOnSourceMeter - 100} ) = VarsOnSourceMeter;
						TempVarsOnSourceMeter( {MaxVarsOnSourceMeter - 100 + 1,MaxVarsOnSourceMeter} ) = 0;
						VarsOnSourceMeter.deallocate();
						VarsOnSourceMeter.allocate( MaxVarsOnSourceMeter );
						VarsOnSourceMeter = TempVarsOnSourceMeter;
						TempVarsOnSourceMeter.deallocate();
					}
					VarsOnSourceMeter( NumVarsOnSourceMeter ) = VarMeterArrays( iKey ).RepVariable;
					continue;
				}
			}

			for ( fldIndex = 4; fldIndex <= NumAlpha; fldIndex += 2 ) {
				if ( cAlphaArgs( fldIndex ) == "*" || lAlphaFieldBlanks( fldIndex ) ) {
					KeyIsStar = true;
					cAlphaArgs( fldIndex ) = "*";
				} else {
					KeyIsStar = false;
				}
				if ( lAlphaFieldBlanks( fldIndex + 1 ) ) {
					ShowSevereError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", blank " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "." );
					ShowContinueError( "...cannot create custom meter." );
					BigErrorsFound = true;
					continue;
				}
				if ( BigErrorsFound ) continue;
				Tagged = false;
				lbrackPos = index( cAlphaArgs( fldIndex + 1 ), "[" );
				if ( lbrackPos != 0 ) cAlphaArgs( fldIndex + 1 ) = cAlphaArgs( fldIndex + 1 )( {1,lbrackPos - 1} );
				// Don't build/check things out if there were errors anywhere.  Use "GetVariableKeys" to map to actual variables...
				GetVariableKeyCountandType( cAlphaArgs( fldIndex + 1 ), KeyCount, TypeVar, AvgSumVar, StepTypeVar, UnitsVar );
				if ( TypeVar == VarType_NotFound ) {
					ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", invalid " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "=\"" + trim( cAlphaArgs( fldIndex + 1 ) ) + "\"." );
					ShowContinueError( "...will not be shown with the Meter results." );
					continue;
				}
				if ( ! MeterCreated ) {
					MeterUnits = UnitsVar;
					AddMeter( cAlphaArgs( 1 ), UnitsVar, BlankString, BlankString, BlankString, BlankString );
					EnergyMeters( NumEnergyMeters ).TypeOfMeter = MeterType_CustomDec;
					EnergyMeters( NumEnergyMeters ).SourceMeter = WhichMeter;

					// Can't use resource type in AddMeter cause it will confuse it with other meters.  So, now:
					GetStandardMeterResourceType( EnergyMeters( NumEnergyMeters ).ResourceType, MakeUPPERCase( cAlphaArgs( 2 ) ), errFlag );
					if ( errFlag ) {
						ShowContinueError( "..on " + trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\"." );
						BigErrorsFound = true;
					}
					DetermineMeterIPUnits( EnergyMeters( NumEnergyMeters ).RT_forIPUnits, EnergyMeters( NumEnergyMeters ).ResourceType, UnitsVar, errFlag );
					if ( errFlag ) {
						ShowContinueError( "..on " + trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\"." );
						ShowContinueError( "..requests for IP units from this meter will be ignored." );
					}
					//        EnergyMeters(NumEnergyMeters)%RT_forIPUnits=DetermineMeterIPUnits(EnergyMeters(NumEnergyMeters)%ResourceType,UnitsVar)
					MeterCreated = true;
				}
				if ( UnitsVar != MeterUnits ) {
					ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", differing units in " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "=\"" + trim( cAlphaArgs( fldIndex + 1 ) ) + "\"." );
					ShowContinueError( "...will not be shown with the Meter results; units for meter=" + trim( MeterUnits ) + ", units for this variable=" + trim( UnitsVar ) + "." );
					continue;
				}
				if ( ( TypeVar == VarType_Real || TypeVar == VarType_Integer ) && AvgSumVar == SummedVar ) {
					Tagged = true;
					NamesOfKeys.allocate( KeyCount );
					IndexesForKeyVar.allocate( KeyCount );
					GetVariableKeys( cAlphaArgs( fldIndex + 1 ), TypeVar, NamesOfKeys, IndexesForKeyVar );
					iOnMeter = 0;
					if ( KeyIsStar ) {
						for ( iKey = 1; iKey <= KeyCount; ++iKey ) {
							++NumVarsOnCustomMeter;
							if ( NumVarsOnCustomMeter > MaxVarsOnCustomMeter ) {
								MaxVarsOnCustomMeter += 100;
								TempVarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
								TempVarsOnCustomMeter( {1,MaxVarsOnCustomMeter - 100} ) = VarsOnCustomMeter;
								TempVarsOnCustomMeter( {MaxVarsOnCustomMeter - 100 + 1,MaxVarsOnCustomMeter} ) = 0;
								VarsOnCustomMeter.deallocate();
								VarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
								VarsOnCustomMeter = TempVarsOnCustomMeter;
								TempVarsOnCustomMeter.deallocate();
							}
							VarsOnCustomMeter( NumVarsOnCustomMeter ) = IndexesForKeyVar( iKey );
							iOnMeter = 1;
						}
						if ( iOnMeter == 0 ) {
							ShowSevereError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", invalid (all keys) " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "=\"" + trim( cAlphaArgs( fldIndex + 1 ) ) + "\"." );
							ErrorsFound = true;
						}
					} else {
						for ( iKey = 1; iKey <= KeyCount; ++iKey ) {
							if ( NamesOfKeys( iKey ) != cAlphaArgs( fldIndex ) ) continue;
							++NumVarsOnCustomMeter;
							if ( NumVarsOnCustomMeter > MaxVarsOnCustomMeter ) {
								MaxVarsOnCustomMeter += 100;
								TempVarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
								TempVarsOnCustomMeter( {1,MaxVarsOnCustomMeter - 100} ) = VarsOnCustomMeter;
								TempVarsOnCustomMeter( {MaxVarsOnCustomMeter - 100 + 1,MaxVarsOnCustomMeter} ) = 0;
								VarsOnCustomMeter.deallocate();
								VarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
								VarsOnCustomMeter = TempVarsOnCustomMeter;
								TempVarsOnCustomMeter.deallocate();
							}
							VarsOnCustomMeter( NumVarsOnCustomMeter ) = IndexesForKeyVar( iKey );
							iOnMeter = 1;
						}
						if ( iOnMeter == 0 ) {
							ShowSevereError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", invalid " + trim( cAlphaArgs( fldIndex ) ) + ":" + trim( cAlphaArgs( fldIndex + 1 ) ) );
							ErrorsFound = true;
						}
					}
					NamesOfKeys.deallocate();
					IndexesForKeyVar.deallocate();
				}
				if ( TypeVar == VarType_Meter && AvgSumVar == SummedVar ) {
					Tagged = true;
					NamesOfKeys.allocate( KeyCount );
					IndexesForKeyVar.allocate( KeyCount );
					GetVariableKeys( cAlphaArgs( fldIndex + 1 ), TypeVar, NamesOfKeys, IndexesForKeyVar );
					WhichMeter = IndexesForKeyVar( 1 );
					NamesOfKeys.deallocate();
					IndexesForKeyVar.deallocate();
					// for meters there will only be one key...  but it has variables associated...
					for ( iOnMeter = 1; iOnMeter <= NumVarMeterArrays; ++iOnMeter ) {
						testa = any_eq( VarMeterArrays( iOnMeter ).OnMeters, WhichMeter );
						testb = false;
						if ( VarMeterArrays( iOnMeter ).NumOnCustomMeters > 0 ) {
							testb = any_eq( VarMeterArrays( iOnMeter ).OnCustomMeters, WhichMeter );
						}
						if ( ! ( testa || testb ) ) continue;
						++NumVarsOnCustomMeter;
						if ( NumVarsOnCustomMeter > MaxVarsOnCustomMeter ) {
							MaxVarsOnCustomMeter += 100;
							TempVarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
							TempVarsOnCustomMeter( {1,MaxVarsOnCustomMeter - 100} ) = VarsOnCustomMeter;
							TempVarsOnCustomMeter( {MaxVarsOnCustomMeter - 100 + 1,MaxVarsOnCustomMeter} ) = 0;
							VarsOnCustomMeter.deallocate();
							VarsOnCustomMeter.allocate( MaxVarsOnCustomMeter );
							VarsOnCustomMeter = TempVarsOnCustomMeter;
							TempVarsOnCustomMeter.deallocate();
						}
						VarsOnCustomMeter( NumVarsOnCustomMeter ) = VarMeterArrays( iOnMeter ).RepVariable;
					}
				}
				if ( ! Tagged ) { // couldn't find place for this item on a meter
					if ( AvgSumVar != SummedVar ) {
						ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", variable not summed variable " + trim( cAlphaFieldNames( fldIndex + 1 ) ) + "=\"" + trim( cAlphaArgs( fldIndex + 1 ) ) + "\"." );
						ShowContinueError( "...will not be shown with the Meter results; units for meter=" + trim( MeterUnits ) + ", units for this variable=" + trim( UnitsVar ) + "." );
					}
				}
			}
			// Check for duplicates
			for ( iKey = 1; iKey <= NumVarsOnCustomMeter; ++iKey ) {
				if ( VarsOnCustomMeter( iKey ) == 0 ) continue;
				for ( iKey1 = iKey + 1; iKey1 <= NumVarsOnCustomMeter; ++iKey1 ) {
					if ( iKey == iKey1 ) continue;
					if ( VarsOnCustomMeter( iKey ) != VarsOnCustomMeter( iKey1 ) ) continue;
					ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", duplicate name=\"" + trim( RVariableTypes( VarsOnCustomMeter( iKey1 ) ).VarName ) + "\"." );
					ShowContinueError( "...only one value with this name will be shown with the Meter results." );
					VarsOnCustomMeter( iKey1 ) = 0;
				}
			}
			for ( iKey = 1; iKey <= NumVarsOnCustomMeter; ++iKey ) {
				if ( VarsOnCustomMeter( iKey ) == 0 ) continue;
				RVariable >>= RVariableTypes( VarsOnCustomMeter( iKey ) ).VarPtr;
				AttachCustomMeters( MeterUnits, VarsOnCustomMeter( iKey ), RVariable().MeterArrayPtr, NumEnergyMeters, ErrorsFound );
			}

			errFlag = false;
			for ( iKey = 1; iKey <= NumVarsOnCustomMeter; ++iKey ) {
				for ( iKey1 = 1; iKey1 <= NumVarsOnSourceMeter; ++iKey1 ) {
					if ( any_eq( VarsOnSourceMeter, VarsOnCustomMeter( iKey ) ) ) break;
					if ( ! errFlag ) {
						ShowSevereError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", invalid specification to " + trim( cAlphaFieldNames( 3 ) ) + "=\"" + trim( cAlphaArgs( 3 ) ) + "\"." );
						errFlag = true;
					}
					ShowContinueError( "..Variable=" + trim( RVariableTypes( VarsOnCustomMeter( iKey ) ).VarName ) );
					ErrorsFound = true;
					break;
				}
			}
			if ( NumVarsOnCustomMeter == 0 ) {
				ShowWarningError( trim( cCurrentModuleObject ) + "=\"" + trim( cAlphaArgs( 1 ) ) + "\", no items assigned " );
				ShowContinueError( "...will not be shown with the Meter results" );
			}

			VarsOnCustomMeter.deallocate();
			VarsOnSourceMeter.deallocate();
		}

		if ( BigErrorsFound ) ErrorsFound = true;

	}

	void
	GetStandardMeterResourceType(
		Fstring & OutResourceType,
		Fstring const & UserInputResourceType,
		bool & ErrorsFound
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   April 2006
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This routine compares the user input resource type with valid ones and returns
		// the standard resource type.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::MakeUPPERCase;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		// na

		ErrorsFound = false;

		//!!! Basic ResourceType for Meters
		{ auto const SELECT_CASE_var( MakeUPPERCase( UserInputResourceType ) );

		if ( ( SELECT_CASE_var == "ELECTRICITY" ) || ( SELECT_CASE_var == "ELECTRIC" ) || ( SELECT_CASE_var == "ELEC" ) ) {
			OutResourceType = "Electricity";

		} else if ( ( SELECT_CASE_var == "GAS" ) || ( SELECT_CASE_var == "NATURALGAS" ) || ( SELECT_CASE_var == "NATURAL GAS" ) ) {
			OutResourceType = "Gas";

		} else if ( SELECT_CASE_var == "GASOLINE" ) {
			OutResourceType = "Gasoline";

		} else if ( SELECT_CASE_var == "DIESEL" ) {
			OutResourceType = "Diesel";

		} else if ( SELECT_CASE_var == "COAL" ) {
			OutResourceType = "Coal";

		} else if ( ( SELECT_CASE_var == "FUEL OIL #1" ) || ( SELECT_CASE_var == "FUELOIL#1" ) || ( SELECT_CASE_var == "FUEL OIL" ) || ( SELECT_CASE_var == "DISTILLATE OIL" ) ) {
			OutResourceType = "FuelOil#1";

		} else if ( ( SELECT_CASE_var == "FUEL OIL #2" ) || ( SELECT_CASE_var == "FUELOIL#2" ) || ( SELECT_CASE_var == "RESIDUAL OIL" ) ) {
			OutResourceType = "FuelOil#2";

		} else if ( ( SELECT_CASE_var == "PROPANE" ) || ( SELECT_CASE_var == "LPG" ) || ( SELECT_CASE_var == "PROPANEGAS" ) || ( SELECT_CASE_var == "PROPANE GAS" ) ) {
			OutResourceType = "Propane";

		} else if ( ( SELECT_CASE_var == "WATER" ) || ( SELECT_CASE_var == "H2O" ) ) {
			OutResourceType = "Water"; // this is water "use"

		} else if ( ( SELECT_CASE_var == "ONSITEWATER" ) || ( SELECT_CASE_var == "WATERPRODUCED" ) || ( SELECT_CASE_var == "ONSITE WATER" ) ) {
			OutResourceType = "OnSiteWater"; // these are for supply record keeping

		} else if ( ( SELECT_CASE_var == "MAINSWATER" ) || ( SELECT_CASE_var == "WATERSUPPLY" ) ) {
			OutResourceType = "MainsWater"; // record keeping

		} else if ( ( SELECT_CASE_var == "RAINWATER" ) || ( SELECT_CASE_var == "PRECIPITATION" ) ) {
			OutResourceType = "RainWater"; // record keeping

		} else if ( ( SELECT_CASE_var == "WELLWATER" ) || ( SELECT_CASE_var == "GROUNDWATER" ) ) {
			OutResourceType = "WellWater"; // record keeping

		} else if ( SELECT_CASE_var == "CONDENSATE" ) {
			OutResourceType = "Condensate"; // record keeping

		} else if ( ( SELECT_CASE_var == "ENERGYTRANSFER" ) || ( SELECT_CASE_var == "ENERGYXFER" ) || ( SELECT_CASE_var == "XFER" ) ) {
			OutResourceType = "EnergyTransfer";

		} else if ( SELECT_CASE_var == "STEAM" ) {
			OutResourceType = "Steam";

		} else if ( SELECT_CASE_var == "DISTRICTCOOLING" ) {
			OutResourceType = "DistrictCooling";

		} else if ( SELECT_CASE_var == "DISTRICTHEATING" ) {
			OutResourceType = "DistrictHeating";

		} else if ( SELECT_CASE_var == "ELECTRICITYPRODUCED" ) {
			OutResourceType = "ElectricityProduced";

		} else if ( SELECT_CASE_var == "ELECTRICITYPURCHASED" ) {
			OutResourceType = "ElectricityPurchased";

		} else if ( SELECT_CASE_var == "ELECTRICITYSURPLUSSOLD" ) {
			OutResourceType = "ElectricitySurplusSold";

		} else if ( SELECT_CASE_var == "ELECTRICITYNET" ) {
			OutResourceType = "ElectricityNet";

		} else if ( SELECT_CASE_var == "SOLARWATER" ) {
			OutResourceType = "SolarWater";

		} else if ( SELECT_CASE_var == "SOLARAIR" ) {
			OutResourceType = "SolarAir";

		} else if ( SELECT_CASE_var == "SO2" ) {
			OutResourceType = "SO2";

		} else if ( SELECT_CASE_var == "NOX" ) {
			OutResourceType = "NOx";

		} else if ( SELECT_CASE_var == "N2O" ) {
			OutResourceType = "N2O";

		} else if ( SELECT_CASE_var == "PM" ) {
			OutResourceType = "PM";

		} else if ( SELECT_CASE_var == "PM2.5" ) {
			OutResourceType = "PM2.5";

		} else if ( SELECT_CASE_var == "PM10" ) {
			OutResourceType = "PM10";

		} else if ( SELECT_CASE_var == "CO" ) {
			OutResourceType = "CO";

		} else if ( SELECT_CASE_var == "CO2" ) {
			OutResourceType = "CO2";

		} else if ( SELECT_CASE_var == "CH4" ) {
			OutResourceType = "CH4";

		} else if ( SELECT_CASE_var == "NH3" ) {
			OutResourceType = "NH3";

		} else if ( SELECT_CASE_var == "NMVOC" ) {
			OutResourceType = "NMVOC";

		} else if ( SELECT_CASE_var == "HG" ) {
			OutResourceType = "Hg";

		} else if ( SELECT_CASE_var == "PB" ) {
			OutResourceType = "Pb";

		} else if ( SELECT_CASE_var == "NUCLEAR HIGH" ) {
			OutResourceType = "Nuclear High";

		} else if ( SELECT_CASE_var == "NUCLEAR LOW" ) {
			OutResourceType = "Nuclear Low";

		} else if ( SELECT_CASE_var == "WATERENVIRONMENTALFACTORS" ) {
			OutResourceType = "WaterEnvironmentalFactors";

		} else if ( SELECT_CASE_var == "CARBON EQUIVALENT" ) {
			OutResourceType = "Carbon Equivalent";

		} else if ( SELECT_CASE_var == "SOURCE" ) {
			OutResourceType = "Source";

		} else if ( SELECT_CASE_var == "PLANTLOOPHEATINGDEMAND" ) {
			OutResourceType = "PlantLoopHeatingDemand";

		} else if ( SELECT_CASE_var == "PLANTLOOPCOOLINGDEMAND" ) {
			OutResourceType = "PlantLoopCoolingDemand";

		} else if ( SELECT_CASE_var == "GENERIC" ) { // only used by custom meters
			OutResourceType = "Generic";

		} else if ( SELECT_CASE_var == "OTHERFUEL1" ) { // other fuel type (defined by user)
			OutResourceType = "OtherFuel1";

		} else if ( SELECT_CASE_var == "OTHERFUEL2" ) { // other fuel type (defined by user)
			OutResourceType = "OtherFuel2";

		} else {
			ShowSevereError( "GetStandardMeterResourceType: Illegal OutResourceType (for Meters) Entered=" + trim( UserInputResourceType ) );
			ErrorsFound = true;

		}}

	}

	void
	AddMeter(
		Fstring const & Name, // Name for the meter
		Fstring const & MtrUnits, // Units for the meter
		Fstring const & ResourceType, // ResourceType for the meter
		Fstring const & EndUse, // EndUse for the meter
		Fstring const & EndUseSub, // EndUse subcategory for the meter
		Fstring const & Group // Group for the meter
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine adds a meter to the current definition set of meters.  If the maximum has
		// already been reached, a reallocation procedure begins.  This action needs to be done at the
		// start of the simulation, primarily before any output is stored.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::FindItemInList;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Found;
		bool errFlag;

		// Object Data
		FArray1D< MeterType > TempMeters;

		// Make sure this isn't already in the list of meter names

		if ( NumEnergyMeters > 0 ) {
			Found = FindItemInList( Name, EnergyMeters.Name(), NumEnergyMeters );
		} else {
			Found = 0;
		}

		if ( Found == 0 ) {
			if ( NumEnergyMeters > 0 ) {
				TempMeters.allocate( NumEnergyMeters );
				TempMeters( {1,NumEnergyMeters} ) = EnergyMeters;
				EnergyMeters.deallocate();
			}
			EnergyMeters.allocate( NumEnergyMeters + 1 );
			if ( NumEnergyMeters > 0 ) {
				EnergyMeters( {1,NumEnergyMeters} ) = TempMeters;
				TempMeters.deallocate();
			}
			++NumEnergyMeters;
			EnergyMeters( NumEnergyMeters ).Name = Name;
			EnergyMeters( NumEnergyMeters ).ResourceType = ResourceType;
			EnergyMeters( NumEnergyMeters ).EndUse = EndUse;
			EnergyMeters( NumEnergyMeters ).EndUseSub = EndUseSub;
			EnergyMeters( NumEnergyMeters ).Group = Group;
			EnergyMeters( NumEnergyMeters ).Units = MtrUnits;
			EnergyMeters( NumEnergyMeters ).TSValue = 0.0;
			EnergyMeters( NumEnergyMeters ).CurTSValue = 0.0;
			EnergyMeters( NumEnergyMeters ).RptTS = false;
			EnergyMeters( NumEnergyMeters ).RptTSFO = false;
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).TSRptNum );
			gio::write( EnergyMeters( NumEnergyMeters ).TSRptNumChr, "*" ) << EnergyMeters( NumEnergyMeters ).TSRptNum;
			EnergyMeters( NumEnergyMeters ).TSRptNumChr = adjustl( EnergyMeters( NumEnergyMeters ).TSRptNumChr );
			EnergyMeters( NumEnergyMeters ).HRValue = 0.0;
			EnergyMeters( NumEnergyMeters ).HRMaxVal = MaxSetValue;
			EnergyMeters( NumEnergyMeters ).HRMaxValDate = 0;
			EnergyMeters( NumEnergyMeters ).HRMinVal = MinSetValue;
			EnergyMeters( NumEnergyMeters ).HRMinValDate = 0;
			EnergyMeters( NumEnergyMeters ).RptHR = false;
			EnergyMeters( NumEnergyMeters ).RptHRFO = false;
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).HRRptNum );
			gio::write( EnergyMeters( NumEnergyMeters ).HRRptNumChr, "*" ) << EnergyMeters( NumEnergyMeters ).HRRptNum;
			EnergyMeters( NumEnergyMeters ).HRRptNumChr = adjustl( EnergyMeters( NumEnergyMeters ).HRRptNumChr );
			EnergyMeters( NumEnergyMeters ).DYValue = 0.0;
			EnergyMeters( NumEnergyMeters ).DYMaxVal = MaxSetValue;
			EnergyMeters( NumEnergyMeters ).DYMaxValDate = 0;
			EnergyMeters( NumEnergyMeters ).DYMinVal = MinSetValue;
			EnergyMeters( NumEnergyMeters ).DYMinValDate = 0;
			EnergyMeters( NumEnergyMeters ).RptDY = false;
			EnergyMeters( NumEnergyMeters ).RptDYFO = false;
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).DYRptNum );
			gio::write( EnergyMeters( NumEnergyMeters ).DYRptNumChr, "*" ) << EnergyMeters( NumEnergyMeters ).DYRptNum;
			EnergyMeters( NumEnergyMeters ).DYRptNumChr = adjustl( EnergyMeters( NumEnergyMeters ).DYRptNumChr );
			EnergyMeters( NumEnergyMeters ).MNValue = 0.0;
			EnergyMeters( NumEnergyMeters ).MNMaxVal = MaxSetValue;
			EnergyMeters( NumEnergyMeters ).MNMaxValDate = 0;
			EnergyMeters( NumEnergyMeters ).MNMinVal = MinSetValue;
			EnergyMeters( NumEnergyMeters ).MNMinValDate = 0;
			EnergyMeters( NumEnergyMeters ).RptMN = false;
			EnergyMeters( NumEnergyMeters ).RptMNFO = false;
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).MNRptNum );
			gio::write( EnergyMeters( NumEnergyMeters ).MNRptNumChr, "*" ) << EnergyMeters( NumEnergyMeters ).MNRptNum;
			EnergyMeters( NumEnergyMeters ).MNRptNumChr = adjustl( EnergyMeters( NumEnergyMeters ).MNRptNumChr );
			EnergyMeters( NumEnergyMeters ).SMValue = 0.0;
			EnergyMeters( NumEnergyMeters ).SMMaxVal = MaxSetValue;
			EnergyMeters( NumEnergyMeters ).SMMaxValDate = 0;
			EnergyMeters( NumEnergyMeters ).SMMinVal = MinSetValue;
			EnergyMeters( NumEnergyMeters ).SMMinValDate = 0;
			EnergyMeters( NumEnergyMeters ).RptSM = false;
			EnergyMeters( NumEnergyMeters ).RptSMFO = false;
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).SMRptNum );
			gio::write( EnergyMeters( NumEnergyMeters ).SMRptNumChr, "*" ) << EnergyMeters( NumEnergyMeters ).SMRptNum;
			EnergyMeters( NumEnergyMeters ).SMRptNumChr = adjustl( EnergyMeters( NumEnergyMeters ).SMRptNumChr );
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).TSAccRptNum );
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).HRAccRptNum );
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).DYAccRptNum );
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).MNAccRptNum );
			AssignReportNumber( EnergyMeters( NumEnergyMeters ).SMAccRptNum );
		} else {
			ShowFatalError( "Requested to Add Meter which was already present=" + trim( Name ) );
		}
		if ( ResourceType != BlankString ) {
			DetermineMeterIPUnits( EnergyMeters( NumEnergyMeters ).RT_forIPUnits, ResourceType, MtrUnits, errFlag );
			if ( errFlag ) {
				ShowContinueError( "..on Meter=\"" + trim( Name ) + "\"." );
				ShowContinueError( "..requests for IP units from this meter will be ignored." );
			}
			//    EnergyMeters(NumEnergyMeters)%RT_forIPUnits=DetermineMeterIPUnits(ResourceType,MtrUnits)
		}
		//  write(outputfiledebug,'(A)') 'add meter=NM='//TRIM(Name)//'; '//  &
		//     'RS='//TRIM(ResourceType)//'; EU='//TRIM(EndUse)//'; EUS='//  &
		//        TRIM(EndUseSub)//'; GP='//TRIM(Group)//'; UT='//TRIM(MtrUnits)

	}

	void
	AttachMeters(
		Fstring const & MtrUnits, // Units for this meter
		Fstring & ResourceType, // Electricity, Gas, etc.
		Fstring & EndUse, // End-use category (Lights, Heating, etc.)
		Fstring & EndUseSub, // End-use subcategory (user-defined, e.g., General Lights, Task Lights, etc.)
		Fstring & Group, // Group key (Facility, Zone, Building, etc.)
		Fstring const & ZoneName, // Zone key only applicable for Building group
		int const RepVarNum, // Number of this report variable
		int & MeterArrayPtr, // Output set of Pointers to Meters
		bool & ErrorsFound // True if errors in this call
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine determines which meters this variable will be on (if any),
		// sets up the meter pointer arrays, and returns a index value to this array which
		// is stored with the variable.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::FindItem;
		using InputProcessor::SameString;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Found;

		// Object Data
		FArray1D< MeterArrayType > TempMeterArrays;

		if ( SameString( Group, "Building" ) ) {
			ValidateNStandardizeMeterTitles( MtrUnits, ResourceType, EndUse, EndUseSub, Group, ErrorsFound, ZoneName );
		} else {
			ValidateNStandardizeMeterTitles( MtrUnits, ResourceType, EndUse, EndUseSub, Group, ErrorsFound );
		}

		if ( NumVarMeterArrays > 0 ) {
			TempMeterArrays.allocate( NumVarMeterArrays );
			TempMeterArrays( {1,NumVarMeterArrays} ) = VarMeterArrays;
			VarMeterArrays.deallocate();
		}
		VarMeterArrays.allocate( NumVarMeterArrays + 1 );
		if ( NumVarMeterArrays > 0 ) {
			VarMeterArrays( {1,NumVarMeterArrays} ) = TempMeterArrays;
			TempMeterArrays.deallocate();
		}
		++NumVarMeterArrays;
		MeterArrayPtr = NumVarMeterArrays;
		VarMeterArrays( NumVarMeterArrays ).NumOnMeters = 0;
		VarMeterArrays( NumVarMeterArrays ).RepVariable = RepVarNum;
		VarMeterArrays( NumVarMeterArrays ).OnMeters = 0;
		Found = FindItem( trim( ResourceType ) + ":Facility", EnergyMeters.Name(), NumEnergyMeters );
		if ( Found != 0 ) {
			++VarMeterArrays( NumVarMeterArrays ).NumOnMeters;
			VarMeterArrays( NumVarMeterArrays ).OnMeters( VarMeterArrays( NumVarMeterArrays ).NumOnMeters ) = Found;
		}
		if ( Group != BlankString ) {
			Found = FindItem( trim( ResourceType ) + ":" + trim( Group ), EnergyMeters.Name(), NumEnergyMeters );
			if ( Found != 0 ) {
				++VarMeterArrays( NumVarMeterArrays ).NumOnMeters;
				VarMeterArrays( NumVarMeterArrays ).OnMeters( VarMeterArrays( NumVarMeterArrays ).NumOnMeters ) = Found;
			}
			if ( SameString( Group, "Building" ) ) { // Match to Zone
				Found = FindItem( trim( ResourceType ) + ":Zone:" + trim( ZoneName ), EnergyMeters.Name(), NumEnergyMeters );
				if ( Found != 0 ) {
					++VarMeterArrays( NumVarMeterArrays ).NumOnMeters;
					VarMeterArrays( NumVarMeterArrays ).OnMeters( VarMeterArrays( NumVarMeterArrays ).NumOnMeters ) = Found;
				}
			}
		}

		//!! Following if EndUse is by ResourceType
		if ( EndUse != BlankString ) {
			Found = FindItem( trim( EndUse ) + ":" + trim( ResourceType ), EnergyMeters.Name(), NumEnergyMeters );
			if ( Found != 0 ) {
				++VarMeterArrays( NumVarMeterArrays ).NumOnMeters;
				VarMeterArrays( NumVarMeterArrays ).OnMeters( VarMeterArrays( NumVarMeterArrays ).NumOnMeters ) = Found;
			}
			if ( SameString( Group, "Building" ) ) { // Match to Zone
				Found = FindItem( trim( EndUse ) + ":" + trim( ResourceType ) + ":Zone:" + trim( ZoneName ), EnergyMeters.Name(), NumEnergyMeters );
				if ( Found != 0 ) {
					++VarMeterArrays( NumVarMeterArrays ).NumOnMeters;
					VarMeterArrays( NumVarMeterArrays ).OnMeters( VarMeterArrays( NumVarMeterArrays ).NumOnMeters ) = Found;
				}
			}

			// End use subcategory
			if ( EndUseSub != BlankString ) {
				Found = FindItem( trim( EndUseSub ) + ":" + trim( EndUse ) + ":" + trim( ResourceType ), EnergyMeters.Name(), NumEnergyMeters );
				if ( Found != 0 ) {
					++VarMeterArrays( NumVarMeterArrays ).NumOnMeters;
					VarMeterArrays( NumVarMeterArrays ).OnMeters( VarMeterArrays( NumVarMeterArrays ).NumOnMeters ) = Found;

					AddEndUseSubcategory( ResourceType, EndUse, EndUseSub );
				}
				if ( SameString( Group, "Building" ) ) { // Match to Zone
					Found = FindItem( trim( EndUseSub ) + ":" + trim( EndUse ) + ":" + trim( ResourceType ) + ":Zone:" + trim( ZoneName ), EnergyMeters.Name(), NumEnergyMeters );
					if ( Found != 0 ) {
						++VarMeterArrays( NumVarMeterArrays ).NumOnMeters;
						VarMeterArrays( NumVarMeterArrays ).OnMeters( VarMeterArrays( NumVarMeterArrays ).NumOnMeters ) = Found;
					}
				}
			}

		}

	}

	void
	AttachCustomMeters(
		Fstring const & MtrUnits, // Units for this meter
		int const RepVarNum, // Number of this report variable
		int & MeterArrayPtr, // Input/Output set of Pointers to Meters
		int const MeterIndex, // Which meter this is
		bool & ErrorsFound // True if errors in this call
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2006
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine determines which meters this variable will be on (if any),
		// sets up the meter pointer arrays, and returns a index value to this array which
		// is stored with the variable.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::FindItem;
		using InputProcessor::SameString;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		FArray1D_int TempOnCustomMeters;

		// Object Data
		FArray1D< MeterArrayType > TempMeterArrays;

		if ( MeterArrayPtr == 0 ) {
			if ( NumVarMeterArrays > 0 ) {
				TempMeterArrays.allocate( NumVarMeterArrays );
				TempMeterArrays( {1,NumVarMeterArrays} ) = VarMeterArrays;
				VarMeterArrays.deallocate();
			}
			VarMeterArrays.allocate( NumVarMeterArrays + 1 );
			if ( NumVarMeterArrays > 0 ) {
				VarMeterArrays( {1,NumVarMeterArrays} ) = TempMeterArrays;
				TempMeterArrays.deallocate();
			}
			++NumVarMeterArrays;
			MeterArrayPtr = NumVarMeterArrays;
			VarMeterArrays( NumVarMeterArrays ).NumOnMeters = 0;
			VarMeterArrays( NumVarMeterArrays ).RepVariable = RepVarNum;
			VarMeterArrays( NumVarMeterArrays ).OnMeters = 0;
			VarMeterArrays( NumVarMeterArrays ).OnCustomMeters.allocate( 1 );
			VarMeterArrays( NumVarMeterArrays ).NumOnCustomMeters = 1;
			VarMeterArrays( NumVarMeterArrays ).OnCustomMeters( VarMeterArrays( NumVarMeterArrays ).NumOnCustomMeters ) = MeterIndex;
		} else {
			// MeterArrayPtr set
			if ( VarMeterArrays( MeterArrayPtr ).NumOnCustomMeters > 0 ) {
				TempOnCustomMeters.allocate( VarMeterArrays( MeterArrayPtr ).NumOnCustomMeters + 1 );
				TempOnCustomMeters( {1,VarMeterArrays( MeterArrayPtr ).NumOnCustomMeters} ) = VarMeterArrays( MeterArrayPtr ).OnCustomMeters;
				VarMeterArrays( MeterArrayPtr ).OnCustomMeters.deallocate();
				++VarMeterArrays( MeterArrayPtr ).NumOnCustomMeters;
				VarMeterArrays( MeterArrayPtr ).OnCustomMeters.allocate( VarMeterArrays( MeterArrayPtr ).NumOnCustomMeters );
				VarMeterArrays( MeterArrayPtr ).OnCustomMeters = TempOnCustomMeters;
				VarMeterArrays( MeterArrayPtr ).OnCustomMeters( VarMeterArrays( MeterArrayPtr ).NumOnCustomMeters ) = MeterIndex;
				TempOnCustomMeters.deallocate();
			} else {
				VarMeterArrays( MeterArrayPtr ).OnCustomMeters.allocate( 1 );
				VarMeterArrays( MeterArrayPtr ).NumOnCustomMeters = 1;
				VarMeterArrays( MeterArrayPtr ).OnCustomMeters( VarMeterArrays( MeterArrayPtr ).NumOnCustomMeters ) = MeterIndex;
			}
		}

	}

	void
	ValidateNStandardizeMeterTitles(
		Fstring const & MtrUnits, // Units for the meter
		Fstring & ResourceType, // Electricity, Gas, etc.
		Fstring & EndUse, // End Use Type (Lights, Heating, etc.)
		Fstring & EndUseSub, // End Use Sub Type (General Lights, Task Lights, etc.)
		Fstring & Group, // Group key (Facility, Zone, Building, etc.)
		bool & ErrorsFound, // True if errors in this call
		Optional_Fstring_const ZoneName // ZoneName when Group=Building
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine uses the keys for the Energy Meters given to the SetupOutputVariable routines
		// and makes sure they are "standard" as well as creating meters which need to be added as this
		// is the first use of that kind of meter designation.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::MakeUPPERCase;
		using InputProcessor::FindItem;
		using DataHeatBalance::Zone;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Found; // For checking whether meter is already defined
		bool LocalErrorsFound;
		Fstring MeterName( MaxNameLength * 2 );

		LocalErrorsFound = false;
		//!!! Basic ResourceType Meters
		GetStandardMeterResourceType( ResourceType, MakeUPPERCase( ResourceType ), LocalErrorsFound );

		if ( ! LocalErrorsFound ) {
			if ( NumEnergyMeters > 0 ) {
				Found = FindItem( trim( ResourceType ) + ":Facility", EnergyMeters.Name(), NumEnergyMeters );
			} else {
				Found = 0;
			}
			if ( Found == 0 ) AddMeter( trim( ResourceType ) + ":Facility", MtrUnits, ResourceType, " ", " ", " " );
		}

		//!!!  Group Meters
		{ auto const SELECT_CASE_var( MakeUPPERCase( Group ) );

		if ( SELECT_CASE_var == BlankString ) {

		} else if ( SELECT_CASE_var == "BUILDING" ) {
			Group = "Building";

		} else if ( ( SELECT_CASE_var == "HVAC" ) || ( SELECT_CASE_var == "SYSTEM" ) ) {
			Group = "HVAC";

		} else if ( SELECT_CASE_var == "PLANT" ) {
			Group = "Plant";

		} else {
			ShowSevereError( "Illegal Group (for Meters) Entered=" + trim( Group ) );
			LocalErrorsFound = true;

		}}

		if ( ! LocalErrorsFound && Group != BlankString ) {
			Found = FindItem( trim( ResourceType ) + ":" + trim( Group ), EnergyMeters.Name(), NumEnergyMeters );
			if ( Found == 0 ) AddMeter( trim( ResourceType ) + ":" + trim( Group ), MtrUnits, ResourceType, " ", " ", Group );
			if ( Group == "Building" ) {
				Found = FindItem( trim( ResourceType ) + ":Zone:" + trim( ZoneName ), EnergyMeters.Name(), NumEnergyMeters );
				if ( Found == 0 ) {
					AddMeter( trim( ResourceType ) + ":Zone:" + trim( ZoneName ), MtrUnits, ResourceType, " ", " ", "Zone" );
				}
			}
		}

		//!!! EndUse Meters
		{ auto const SELECT_CASE_var( MakeUPPERCase( EndUse ) );

		if ( SELECT_CASE_var == BlankString ) {

		} else if ( ( SELECT_CASE_var == "INTERIOR LIGHTS" ) || ( SELECT_CASE_var == "INTERIORLIGHTS" ) ) {
			EndUse = "InteriorLights";

		} else if ( ( SELECT_CASE_var == "EXTERIOR LIGHTS" ) || ( SELECT_CASE_var == "EXTERIORLIGHTS" ) ) {
			EndUse = "ExteriorLights";

		} else if ( ( SELECT_CASE_var == "HEATING" ) || ( SELECT_CASE_var == "HTG" ) ) {
			EndUse = "Heating";

		} else if ( SELECT_CASE_var == "HEATPRODUCED" ) {
			EndUse = "HeatProduced";

		} else if ( ( SELECT_CASE_var == "COOLING" ) || ( SELECT_CASE_var == "CLG" ) ) {
			EndUse = "Cooling";

		} else if ( ( SELECT_CASE_var == "DOMESTICHOTWATER" ) || ( SELECT_CASE_var == "DHW" ) || ( SELECT_CASE_var == "DOMESTIC HOT WATER" ) ) {
			EndUse = "WaterSystems";

		} else if ( ( SELECT_CASE_var == "COGEN" ) || ( SELECT_CASE_var == "COGENERATION" ) ) {
			EndUse = "Cogeneration";

		} else if ( ( SELECT_CASE_var == "INTERIOREQUIPMENT" ) || ( SELECT_CASE_var == "INTERIOR EQUIPMENT" ) ) {
			EndUse = "InteriorEquipment";

		} else if ( ( SELECT_CASE_var == "EXTERIOREQUIPMENT" ) || ( SELECT_CASE_var == "EXTERIOR EQUIPMENT" ) || ( SELECT_CASE_var == "EXT EQ" ) || ( SELECT_CASE_var == "EXTERIOREQ" ) ) {
			EndUse = "ExteriorEquipment";

		} else if ( SELECT_CASE_var == "EXTERIOR:WATEREQUIPMENT" ) {
			EndUse = "ExteriorEquipment";

		} else if ( ( SELECT_CASE_var == "PURCHASEDHOTWATER" ) || ( SELECT_CASE_var == "DISTRICTHOTWATER" ) || ( SELECT_CASE_var == "PURCHASED HEATING" ) ) {
			EndUse = "DistrictHotWater";

		} else if ( ( SELECT_CASE_var == "PURCHASEDCOLDWATER" ) || ( SELECT_CASE_var == "DISTRICTCHILLEDWATER" ) || ( SELECT_CASE_var == "PURCHASEDCHILLEDWATER" ) || ( SELECT_CASE_var == "PURCHASED COLD WATER" ) || ( SELECT_CASE_var == "PURCHASED COOLING" ) ) {
			EndUse = "DistrictChilledWater";

		} else if ( ( SELECT_CASE_var == "FANS" ) || ( SELECT_CASE_var == "FAN" ) ) {
			EndUse = "Fans";

		} else if ( ( SELECT_CASE_var == "HEATINGCOILS" ) || ( SELECT_CASE_var == "HEATINGCOIL" ) || ( SELECT_CASE_var == "HEATING COILS" ) || ( SELECT_CASE_var == "HEATING COIL" ) ) {
			EndUse = "HeatingCoils";

		} else if ( ( SELECT_CASE_var == "COOLINGCOILS" ) || ( SELECT_CASE_var == "COOLINGCOIL" ) || ( SELECT_CASE_var == "COOLING COILS" ) || ( SELECT_CASE_var == "COOLING COIL" ) ) {
			EndUse = "CoolingCoils";

		} else if ( ( SELECT_CASE_var == "PUMPS" ) || ( SELECT_CASE_var == "PUMP" ) ) {
			EndUse = "Pumps";

		} else if ( ( SELECT_CASE_var == "FREECOOLING" ) || ( SELECT_CASE_var == "FREE COOLING" ) ) {
			EndUse = "Freecooling";

		} else if ( SELECT_CASE_var == "LOOPTOLOOP" ) {
			EndUse = "LoopToLoop";

		} else if ( ( SELECT_CASE_var == "CHILLERS" ) || ( SELECT_CASE_var == "CHILLER" ) ) {
			EndUse = "Chillers";

		} else if ( ( SELECT_CASE_var == "BOILERS" ) || ( SELECT_CASE_var == "BOILER" ) ) {
			EndUse = "Boilers";

		} else if ( ( SELECT_CASE_var == "BASEBOARD" ) || ( SELECT_CASE_var == "BASEBOARDS" ) ) {
			EndUse = "Baseboard";

		} else if ( ( SELECT_CASE_var == "HEATREJECTION" ) || ( SELECT_CASE_var == "HEAT REJECTION" ) ) {
			EndUse = "HeatRejection";

		} else if ( ( SELECT_CASE_var == "HUMIDIFIER" ) || ( SELECT_CASE_var == "HUMIDIFIERS" ) ) {
			EndUse = "Humidifier";

		} else if ( ( SELECT_CASE_var == "HEATRECOVERY" ) || ( SELECT_CASE_var == "HEAT RECOVERY" ) ) {
			EndUse = "HeatRecovery";

		} else if ( ( SELECT_CASE_var == "PHOTOVOLTAICS" ) || ( SELECT_CASE_var == "PV" ) || ( SELECT_CASE_var == "PHOTOVOLTAIC" ) ) {
			EndUse = "Photovoltaic";

		} else if ( ( SELECT_CASE_var == "WINDTURBINES" ) || ( SELECT_CASE_var == "WT" ) || ( SELECT_CASE_var == "WINDTURBINE" ) ) {
			EndUse = "WindTurbine";

		} else if ( ( SELECT_CASE_var == "HEAT RECOVERY FOR COOLING" ) || ( SELECT_CASE_var == "HEATRECOVERYFORCOOLING" ) || ( SELECT_CASE_var == "HEATRECOVERYCOOLING" ) ) {
			EndUse = "HeatRecoveryForCooling";

		} else if ( ( SELECT_CASE_var == "HEAT RECOVERY FOR HEATING" ) || ( SELECT_CASE_var == "HEATRECOVERYFORHEATING" ) || ( SELECT_CASE_var == "HEATRECOVERYHEATING" ) ) {
			EndUse = "HeatRecoveryForHeating";

		} else if ( SELECT_CASE_var == "ELECTRICEMISSIONS" ) {
			EndUse = "ElectricEmissions";

		} else if ( SELECT_CASE_var == "PURCHASEDELECTRICEMISSIONS" ) {
			EndUse = "PurchasedElectricEmissions";

		} else if ( SELECT_CASE_var == "SOLDELECTRICEMISSIONS" ) {
			EndUse = "SoldElectricEmissions";

		} else if ( SELECT_CASE_var == "NATURALGASEMISSIONS" ) {
			EndUse = "NaturalGasEmissions";

		} else if ( SELECT_CASE_var == "FUELOIL#1EMISSIONS" ) {
			EndUse = "FuelOil#1Emissions";

		} else if ( SELECT_CASE_var == "FUELOIL#2EMISSIONS" ) {
			EndUse = "FuelOil#2Emissions";

		} else if ( SELECT_CASE_var == "COALEMISSIONS" ) {
			EndUse = "CoalEmissions";

		} else if ( SELECT_CASE_var == "GASOLINEEMISSIONS" ) {
			EndUse = "GasolineEmissions";

		} else if ( SELECT_CASE_var == "PROPANEEMISSIONS" ) {
			EndUse = "PropaneEmissions";

		} else if ( SELECT_CASE_var == "DIESELEMISSIONS" ) {
			EndUse = "DieselEmissions";

		} else if ( SELECT_CASE_var == "OTHERFUEL1EMISSIONS" ) {
			EndUse = "OtherFuel1Emissions";

		} else if ( SELECT_CASE_var == "OTHERFUEL2EMISSIONS" ) {
			EndUse = "OtherFuel2Emissions";

		} else if ( SELECT_CASE_var == "CARBONEQUIVALENTEMISSIONS" ) {
			EndUse = "CarbonEquivalentEmissions";

		} else if ( SELECT_CASE_var == "REFRIGERATION" ) {
			EndUse = "Refrigeration";

		} else if ( SELECT_CASE_var == "COLDSTORAGECHARGE" ) {
			EndUse = "ColdStorageCharge";

		} else if ( SELECT_CASE_var == "COLDSTORAGEDISCHARGE" ) {
			EndUse = "ColdStorageDischarge";

		} else if ( ( SELECT_CASE_var == "WATERSYSTEMS" ) || ( SELECT_CASE_var == "WATERSYSTEM" ) || ( SELECT_CASE_var == "Water System" ) ) {
			EndUse = "WaterSystems";

		} else if ( SELECT_CASE_var == "RAINWATER" ) {
			EndUse = "Rainwater";

		} else if ( SELECT_CASE_var == "CONDENSATE" ) {
			EndUse = "Condensate";

		} else if ( SELECT_CASE_var == "WELLWATER" ) {
			EndUse = "Wellwater";

		} else if ( ( SELECT_CASE_var == "MAINSWATER" ) || ( SELECT_CASE_var == "PURCHASEDWATER" ) ) {
			EndUse = "MainsWater";

		} else {
			ShowSevereError( "Illegal EndUse (for Meters) Entered=" + trim( EndUse ) );
			LocalErrorsFound = true;

		}}

		//!! Following if we do EndUse by ResourceType
		if ( ! LocalErrorsFound && EndUse != BlankString ) {
			Found = FindItem( trim( EndUse ) + ":" + trim( ResourceType ), EnergyMeters.Name(), NumEnergyMeters );
			if ( Found == 0 ) AddMeter( trim( EndUse ) + ":" + trim( ResourceType ), MtrUnits, ResourceType, EndUse, " ", " " );

			if ( Group == "Building" ) { // Match to Zone
				Found = FindItem( trim( EndUse ) + ":" + trim( ResourceType ) + ":Zone:" + trim( ZoneName ), EnergyMeters.Name(), NumEnergyMeters );
				if ( Found == 0 ) {
					AddMeter( trim( EndUse ) + ":" + trim( ResourceType ) + ":Zone:" + trim( ZoneName ), MtrUnits, ResourceType, EndUse, " ", "Zone" );
				}
			}
		} else if ( LocalErrorsFound ) {
			ErrorsFound = true;
		}

		// End-Use Subcategories
		if ( ! LocalErrorsFound && EndUseSub != BlankString ) {
			MeterName = trim( EndUseSub ) + ":" + trim( EndUse ) + ":" + trim( ResourceType );
			Found = FindItem( MeterName, EnergyMeters.Name(), NumEnergyMeters );
			if ( Found == 0 ) AddMeter( MeterName, MtrUnits, ResourceType, EndUse, EndUseSub, " " );
		} else if ( LocalErrorsFound ) {
			ErrorsFound = true;
		}

	}

	void
	DetermineMeterIPUnits(
		int & CodeForIPUnits, // Output Code for IP Units
		Fstring const & ResourceType, // Resource Type
		Fstring const & MtrUnits, // Meter units
		bool & ErrorsFound // true if errors found during subroutine
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2012
		//       MODIFIED       September 2012; made into subroutine
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// In order to set up tabular reports for IP units, need to search on same strings
		// that tabular reports does for IP conversion.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// OutputReportTabular looks for:
		// CONSUMP - not used in meters
		// ELEC - Electricity (kWH)
		// GAS - Gas (therm)
		// COOL - Cooling (ton)
		// and we need to add WATER (for m3/gal, etc)

		// Using/Aliasing
		using InputProcessor::MakeUPPERCase;
		using InputProcessor::SameString;
		//  USE DataGlobals, ONLY: outputfiledebug

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring UC_ResourceType( MaxNameLength );

		ErrorsFound = false;
		UC_ResourceType = MakeUPPERCase( ResourceType );

		CodeForIPUnits = RT_IPUnits_OtherJ;
		if ( index( UC_ResourceType, "ELEC" ) > 0 ) {
			CodeForIPUnits = RT_IPUnits_Electricity;
		} else if ( index( UC_ResourceType, "GAS" ) > 0 ) {
			CodeForIPUnits = RT_IPUnits_Gas;
		} else if ( index( UC_ResourceType, "COOL" ) > 0 ) {
			CodeForIPUnits = RT_IPUnits_Cooling;
		}
		if ( SameString( MtrUnits, "m3" ) && index( UC_ResourceType, "WATER" ) > 0 ) {
			CodeForIPUnits = RT_IPUnits_Water;
		} else if ( SameString( MtrUnits, "m3" ) ) {
			CodeForIPUnits = RT_IPUnits_OtherM3;
		}
		if ( SameString( MtrUnits, "kg" ) ) {
			CodeForIPUnits = RT_IPUnits_OtherKG;
		}
		if ( SameString( MtrUnits, "L" ) ) {
			CodeForIPUnits = RT_IPUnits_OtherL;
		}
		//  write(outputfiledebug,*) 'resourcetype=',TRIM(resourcetype)
		//  write(outputfiledebug,*) 'ipunits type=',CodeForIPUnits
		if ( ! SameString( MtrUnits, "kg" ) && ! SameString( MtrUnits, "J" ) && ! SameString( MtrUnits, "m3" ) && ! SameString( MtrUnits, "L" ) ) {
			ShowWarningError( "DetermineMeterIPUnits: Meter units not recognized for IP Units conversion=[" + trim( MtrUnits ) + "]." );
			ErrorsFound = true;
		}

	}

	void
	UpdateMeterValues(
		Real64 const TimeStepValue, // Value of this variable at the current time step.
		int const NumOnMeters, // Number of meters this variable is "on".
		FArray1S_int const OnMeters, // Which meters this variable is on (index values)
		Optional_int_const NumOnCustomMeters, // Number of custom meters this variable is "on".
		Optional< FArray1S_int const > OnCustomMeters // Which custom meters this variable is on (index values)
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine updates all the meter values in the lists with the current
		// time step value for this variable.

		// METHODOLOGY EMPLOYED:
		// Variables, as they are "setup", may or may not be on one or more meters.
		// All "metered" variables are on the "facility meter".  Index values will be
		// set from the variables to the appropriate meters.  Then, the updating of
		// the meter values is quite simple -- just add the time step value of the variable
		// (which is passed to this routine) to all the values being kept for the meter.
		// Reporting of the meters is taken care of in a different routine.  During reporting,
		// some values will also be reset (for example, after reporting the "hour", the new
		// "hour" value of the meter is reset to 0.0, etc.

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Argument array dimensioning

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Meter; // Loop Control
		int Which; // Index value for the meter

		for ( Meter = 1; Meter <= NumOnMeters; ++Meter ) {
			Which = OnMeters( Meter );
			MeterValue( Which ) += TimeStepValue;
		}

		// This calculates the basic values for decrement/difference meters -- UpdateMeters then calculates the actual.
		if ( present( NumOnCustomMeters ) ) {
			for ( Meter = 1; Meter <= NumOnCustomMeters; ++Meter ) {
				Which = OnCustomMeters()( Meter );
				MeterValue( Which ) += TimeStepValue;
			}
		}

	}

	void
	UpdateMeters( int const TimeStamp ) // Current TimeStamp (for max/min)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   April 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine updates the meters with the current time step value
		// for each meter.  Also, sets min/max values for hourly...run period reporting.

		// METHODOLOGY EMPLOYED:
		// Goes thru the number of meters, setting min/max as appropriate.  Uses timestamp
		// from calling program.

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Meter; // Loop Control

		for ( Meter = 1; Meter <= NumEnergyMeters; ++Meter ) {
			if ( EnergyMeters( Meter ).TypeOfMeter != MeterType_CustomDec && EnergyMeters( Meter ).TypeOfMeter != MeterType_CustomDiff ) {
				EnergyMeters( Meter ).TSValue += MeterValue( Meter );
				EnergyMeters( Meter ).HRValue += MeterValue( Meter );
				SetMinMax( EnergyMeters( Meter ).TSValue, TimeStamp, EnergyMeters( Meter ).HRMaxVal, EnergyMeters( Meter ).HRMaxValDate, EnergyMeters( Meter ).HRMinVal, EnergyMeters( Meter ).HRMinValDate );
				EnergyMeters( Meter ).DYValue += MeterValue( Meter );
				SetMinMax( EnergyMeters( Meter ).TSValue, TimeStamp, EnergyMeters( Meter ).DYMaxVal, EnergyMeters( Meter ).DYMaxValDate, EnergyMeters( Meter ).DYMinVal, EnergyMeters( Meter ).DYMinValDate );
				EnergyMeters( Meter ).MNValue += MeterValue( Meter );
				SetMinMax( EnergyMeters( Meter ).TSValue, TimeStamp, EnergyMeters( Meter ).MNMaxVal, EnergyMeters( Meter ).MNMaxValDate, EnergyMeters( Meter ).MNMinVal, EnergyMeters( Meter ).MNMinValDate );
				EnergyMeters( Meter ).SMValue += MeterValue( Meter );
				SetMinMax( EnergyMeters( Meter ).TSValue, TimeStamp, EnergyMeters( Meter ).SMMaxVal, EnergyMeters( Meter ).SMMaxValDate, EnergyMeters( Meter ).SMMinVal, EnergyMeters( Meter ).SMMinValDate );
			} else {
				EnergyMeters( Meter ).TSValue = EnergyMeters( EnergyMeters( Meter ).SourceMeter ).TSValue - MeterValue( Meter );
				EnergyMeters( Meter ).HRValue += EnergyMeters( Meter ).TSValue;
				SetMinMax( EnergyMeters( Meter ).TSValue, TimeStamp, EnergyMeters( Meter ).HRMaxVal, EnergyMeters( Meter ).HRMaxValDate, EnergyMeters( Meter ).HRMinVal, EnergyMeters( Meter ).HRMinValDate );
				EnergyMeters( Meter ).DYValue += EnergyMeters( Meter ).TSValue;
				SetMinMax( EnergyMeters( Meter ).TSValue, TimeStamp, EnergyMeters( Meter ).DYMaxVal, EnergyMeters( Meter ).DYMaxValDate, EnergyMeters( Meter ).DYMinVal, EnergyMeters( Meter ).DYMinValDate );
				EnergyMeters( Meter ).MNValue += EnergyMeters( Meter ).TSValue;
				SetMinMax( EnergyMeters( Meter ).TSValue, TimeStamp, EnergyMeters( Meter ).MNMaxVal, EnergyMeters( Meter ).MNMaxValDate, EnergyMeters( Meter ).MNMinVal, EnergyMeters( Meter ).MNMinValDate );
				EnergyMeters( Meter ).SMValue += EnergyMeters( Meter ).TSValue;
				SetMinMax( EnergyMeters( Meter ).TSValue, TimeStamp, EnergyMeters( Meter ).SMMaxVal, EnergyMeters( Meter ).SMMaxValDate, EnergyMeters( Meter ).SMMinVal, EnergyMeters( Meter ).SMMinValDate );
			}
		}

		MeterValue = 0.0; // Ready for next update

	}

	void
	SetMinMax(
		Real64 const TestValue, // Candidate new value
		int const TimeStamp, // TimeStamp to be stored if applicable
		Real64 & CurMaxValue, // Current Maximum Value
		int & CurMaxValDate, // Current Maximum Value Date Stamp
		Real64 & CurMinValue, // Current Minimum Value
		int & CurMinValDate // Current Minimum Value Date Stamp
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine looks at the current value, comparing against the current max and
		// min for this meter/variable and resets along with a timestamp if applicable.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		// na

		if ( TestValue > CurMaxValue ) {
			CurMaxValue = TestValue;
			CurMaxValDate = TimeStamp;
		}
		if ( TestValue < CurMinValue ) {
			CurMinValue = TestValue;
			CurMinValDate = TimeStamp;
		}

	}

	void
	ReportTSMeters(
		Real64 const StartMinute, // Start Minute for TimeStep
		Real64 const EndMinute, // End Minute for TimeStep
		bool & PrintESOTimeStamp // True if the ESO Time Stamp also needs to be printed
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine reports on the meters that have been requested for
		// reporting on each time step.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using General::RemoveTrailingZeros;
		using General::TrimSigDigits;
		using SQLiteProcedures::SQLdbTimeIndex;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop; // Loop Control
		bool PrintTimeStamp;
		int CurDayType;
		static Real64 rDummy1( 0.0 );
		static Real64 rDummy2( 0.0 );
		static int iDummy1( 0 );
		static int iDummy2( 0 );
		Fstring cReportID( 16 );

		PrintTimeStamp = true;
		for ( Loop = 1; Loop <= NumEnergyMeters; ++Loop ) {
			EnergyMeters( Loop ).CurTSValue = EnergyMeters( Loop ).TSValue;
			if ( ! EnergyMeters( Loop ).RptTS && ! EnergyMeters( Loop ).RptAccTS ) continue;
			if ( PrintTimeStamp ) {
				CurDayType = DayOfWeek;
				if ( HolidayIndex > 0 ) {
					CurDayType = 7 + HolidayIndex;
				}
				SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileMeters, ReportEach, TimeStepStampReportNbr, TimeStepStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, HourOfDay, EndMinute, StartMinute, DSTIndicator, DayTypes( CurDayType ) );
				PrintTimeStamp = false;
			}

			if ( PrintESOTimeStamp && ! EnergyMeters( Loop ).RptTSFO && ! EnergyMeters( Loop ).RptAccTSFO ) {
				CurDayType = DayOfWeek;
				if ( HolidayIndex > 0 ) {
					CurDayType = 7 + HolidayIndex;
				}
				SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileStandard, ReportEach, TimeStepStampReportNbr, TimeStepStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, HourOfDay, EndMinute, StartMinute, DSTIndicator, DayTypes( CurDayType ) );
				PrintESOTimeStamp = false;
			}

			if ( EnergyMeters( Loop ).RptTS ) {
				WriteReportMeterData( EnergyMeters( Loop ).TSRptNum, EnergyMeters( Loop ).TSRptNumChr, SQLdbTimeIndex, EnergyMeters( Loop ).TSValue, ReportTimeStep, rDummy1, iDummy1, rDummy2, iDummy2, EnergyMeters( Loop ).RptTSFO );
			}

			if ( EnergyMeters( Loop ).RptAccTS ) {
				gio::write( cReportID, "*" ) << EnergyMeters( Loop ).TSAccRptNum;
				cReportID = adjustl( cReportID );
				WriteCumulativeReportMeterData( EnergyMeters( Loop ).TSAccRptNum, cReportID, SQLdbTimeIndex, EnergyMeters( Loop ).SMValue, EnergyMeters( Loop ).RptAccTSFO );
			}
		}

		if ( NumEnergyMeters > 0 ) {
			EnergyMeters.TSValue() = 0.0;
		}

	}

	void
	ReportHRMeters()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine reports on the meters that have been requested for
		// reporting on each hour.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using General::RemoveTrailingZeros;
		using General::TrimSigDigits;
		using SQLiteProcedures::SQLdbTimeIndex;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop; // Loop Control
		bool PrintTimeStamp;
		int CurDayType;
		static Real64 rDummy1( 0.0 );
		static Real64 rDummy2( 0.0 );
		static int iDummy1( 0 );
		static int iDummy2( 0 );
		Fstring cReportID( 16 );

		PrintTimeStamp = true;
		for ( Loop = 1; Loop <= NumEnergyMeters; ++Loop ) {
			if ( ! EnergyMeters( Loop ).RptHR && ! EnergyMeters( Loop ).RptAccHR ) continue;
			if ( PrintTimeStamp ) {
				CurDayType = DayOfWeek;
				if ( HolidayIndex > 0 ) {
					CurDayType = 7 + HolidayIndex;
				}
				SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileMeters, ReportHourly, TimeStepStampReportNbr, TimeStepStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, HourOfDay, _, _, DSTIndicator, DayTypes( CurDayType ) );
				PrintTimeStamp = false;
			}

			if ( EnergyMeters( Loop ).RptHR ) {
				WriteReportMeterData( EnergyMeters( Loop ).HRRptNum, EnergyMeters( Loop ).HRRptNumChr, SQLdbTimeIndex, EnergyMeters( Loop ).HRValue, ReportHourly, rDummy1, iDummy1, rDummy2, iDummy2, EnergyMeters( Loop ).RptHRFO ); //EnergyMeters(Loop)%HRMinVal, EnergyMeters(Loop)%HRMinValDate, & | EnergyMeters(Loop)%HRMaxVal, EnergyMeters(Loop)%HRMaxValDate, &
				EnergyMeters( Loop ).HRValue = 0.0;
				EnergyMeters( Loop ).HRMinVal = MinSetValue;
				EnergyMeters( Loop ).HRMaxVal = MaxSetValue;
			}

			if ( EnergyMeters( Loop ).RptAccHR ) {
				gio::write( cReportID, "*" ) << EnergyMeters( Loop ).HRAccRptNum;
				cReportID = adjustl( cReportID );
				WriteCumulativeReportMeterData( EnergyMeters( Loop ).HRAccRptNum, cReportID, SQLdbTimeIndex, EnergyMeters( Loop ).SMValue, EnergyMeters( Loop ).RptAccHRFO );
			}
		}

	}

	void
	ReportDYMeters()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine reports on the meters that have been requested for
		// reporting on each day.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using General::RemoveTrailingZeros;
		using General::TrimSigDigits;
		using SQLiteProcedures::SQLdbTimeIndex;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop; // Loop Control
		bool PrintTimeStamp;
		int CurDayType;
		Fstring cReportID( 16 );

		PrintTimeStamp = true;
		for ( Loop = 1; Loop <= NumEnergyMeters; ++Loop ) {
			if ( ! EnergyMeters( Loop ).RptDY && ! EnergyMeters( Loop ).RptAccDY ) continue;
			if ( PrintTimeStamp ) {
				CurDayType = DayOfWeek;
				if ( HolidayIndex > 0 ) {
					CurDayType = 7 + HolidayIndex;
				}
				SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileMeters, ReportDaily, DailyStampReportNbr, DailyStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, _, _, _, DSTIndicator, DayTypes( CurDayType ) );
				PrintTimeStamp = false;
			}

			if ( EnergyMeters( Loop ).RptDY ) {
				WriteReportMeterData( EnergyMeters( Loop ).DYRptNum, EnergyMeters( Loop ).DYRptNumChr, SQLdbTimeIndex, EnergyMeters( Loop ).DYValue, ReportDaily, EnergyMeters( Loop ).DYMinVal, EnergyMeters( Loop ).DYMinValDate, EnergyMeters( Loop ).DYMaxVal, EnergyMeters( Loop ).DYMaxValDate, EnergyMeters( Loop ).RptDYFO );
				EnergyMeters( Loop ).DYValue = 0.0;
				EnergyMeters( Loop ).DYMinVal = MinSetValue;
				EnergyMeters( Loop ).DYMaxVal = MaxSetValue;
			}

			if ( EnergyMeters( Loop ).RptAccDY ) {
				gio::write( cReportID, "*" ) << EnergyMeters( Loop ).DYAccRptNum;
				cReportID = adjustl( cReportID );
				WriteCumulativeReportMeterData( EnergyMeters( Loop ).DYAccRptNum, cReportID, SQLdbTimeIndex, EnergyMeters( Loop ).SMValue, EnergyMeters( Loop ).RptAccDYFO );
			}
		}

	}

	void
	ReportMNMeters()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine reports on the meters that have been requested for
		// reporting on each month.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using General::RemoveTrailingZeros;
		using General::TrimSigDigits;
		using SQLiteProcedures::SQLdbTimeIndex;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop; // Loop Control
		bool PrintTimeStamp;
		Fstring cReportID( 16 );

		PrintTimeStamp = true;
		for ( Loop = 1; Loop <= NumEnergyMeters; ++Loop ) {
			if ( ! EnergyMeters( Loop ).RptMN && ! EnergyMeters( Loop ).RptAccMN ) continue;
			if ( PrintTimeStamp ) {
				SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileMeters, ReportMonthly, MonthlyStampReportNbr, MonthlyStampReportChr, DayOfSim, DayOfSimChr, Month );
				PrintTimeStamp = false;
			}

			if ( EnergyMeters( Loop ).RptMN ) {
				WriteReportMeterData( EnergyMeters( Loop ).MNRptNum, EnergyMeters( Loop ).MNRptNumChr, SQLdbTimeIndex, EnergyMeters( Loop ).MNValue, ReportMonthly, EnergyMeters( Loop ).MNMinVal, EnergyMeters( Loop ).MNMinValDate, EnergyMeters( Loop ).MNMaxVal, EnergyMeters( Loop ).MNMaxValDate, EnergyMeters( Loop ).RptMNFO );
				EnergyMeters( Loop ).MNValue = 0.0;
				EnergyMeters( Loop ).MNMinVal = MinSetValue;
				EnergyMeters( Loop ).MNMaxVal = MaxSetValue;
			}

			if ( EnergyMeters( Loop ).RptAccMN ) {
				gio::write( cReportID, "*" ) << EnergyMeters( Loop ).MNAccRptNum;
				cReportID = adjustl( cReportID );
				WriteCumulativeReportMeterData( EnergyMeters( Loop ).MNAccRptNum, cReportID, SQLdbTimeIndex, EnergyMeters( Loop ).SMValue, EnergyMeters( Loop ).RptAccMNFO );
			}
		}

	}

	void
	ReportSMMeters()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2001
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine reports on the meters that have been requested for
		// reporting on each environment/run period.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using General::RemoveTrailingZeros;
		using General::TrimSigDigits;
		using SQLiteProcedures::SQLdbTimeIndex;
		//using namespace OutputReportPredefined;
		using DataGlobals::OutputFileDebug; // ,DoingPredefinedAndTabularReporting

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		Real64 const convertJtoGJ( 1.0 / 1000000000.0 );

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop; // Loop Control
		bool PrintTimeStamp;
		Fstring cReportID( 16 );

		PrintTimeStamp = true;
		for ( Loop = 1; Loop <= NumEnergyMeters; ++Loop ) {
			EnergyMeters( Loop ).LastSMValue = EnergyMeters( Loop ).SMValue;
			EnergyMeters( Loop ).LastSMMinVal = EnergyMeters( Loop ).SMMinVal;
			EnergyMeters( Loop ).LastSMMinValDate = EnergyMeters( Loop ).SMMinValDate;
			EnergyMeters( Loop ).LastSMMaxVal = EnergyMeters( Loop ).SMMaxVal;
			EnergyMeters( Loop ).LastSMMaxValDate = EnergyMeters( Loop ).SMMaxValDate;
			if ( ! EnergyMeters( Loop ).RptSM && ! EnergyMeters( Loop ).RptAccSM ) continue;
			if ( PrintTimeStamp ) {
				SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileMeters, ReportSim, RunPeriodStampReportNbr, RunPeriodStampReportChr, DayOfSim, DayOfSimChr );
				PrintTimeStamp = false;
			}

			if ( EnergyMeters( Loop ).RptSM ) {
				WriteReportMeterData( EnergyMeters( Loop ).SMRptNum, EnergyMeters( Loop ).SMRptNumChr, SQLdbTimeIndex, EnergyMeters( Loop ).SMValue, ReportSim, EnergyMeters( Loop ).SMMinVal, EnergyMeters( Loop ).SMMinValDate, EnergyMeters( Loop ).SMMaxVal, EnergyMeters( Loop ).SMMaxValDate, EnergyMeters( Loop ).RptSMFO );
			}

			if ( EnergyMeters( Loop ).RptAccSM ) {
				gio::write( cReportID, "*" ) << EnergyMeters( Loop ).SMAccRptNum;
				cReportID = adjustl( cReportID );
				WriteCumulativeReportMeterData( EnergyMeters( Loop ).SMAccRptNum, cReportID, SQLdbTimeIndex, EnergyMeters( Loop ).SMValue, EnergyMeters( Loop ).RptAccSMFO );
			}
		}

		if ( NumEnergyMeters > 0 ) {
			EnergyMeters.SMValue() = 0.0;
			EnergyMeters.SMMinVal() = MinSetValue;
			EnergyMeters.SMMaxVal() = MaxSetValue;
		}

	}

	void
	ReportForTabularReports()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   August 2013
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine is called after all the simulation is done and before
		// tabular reports in order to reduce the number of calls to the predefined routine
		// for SM (Simulation period) meters, the value of the last calculation is stored
		// in the data structure.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using namespace OutputReportPredefined;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		Real64 const convertJtoGJ( 1.0 / 1000000000.0 );

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int Loop; // Loop Control

		for ( Loop = 1; Loop <= NumEnergyMeters; ++Loop ) {
			{ auto const SELECT_CASE_var( EnergyMeters( Loop ).RT_forIPUnits );
			if ( SELECT_CASE_var == RT_IPUnits_Electricity ) {
				PreDefTableEntry( pdchEMelecannual, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMValue * convertJtoGJ );
				PreDefTableEntry( pdchEMelecminvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMinVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMelecminvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMinValDate ) );
				PreDefTableEntry( pdchEMelecmaxvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMaxVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMelecmaxvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMaxValDate ) );
			} else if ( SELECT_CASE_var == RT_IPUnits_Gas ) {
				PreDefTableEntry( pdchEMgasannual, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMValue * convertJtoGJ );
				PreDefTableEntry( pdchEMgasminvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMinVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMgasminvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMinValDate ) );
				PreDefTableEntry( pdchEMgasmaxvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMaxVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMgasmaxvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMaxValDate ) );
			} else if ( SELECT_CASE_var == RT_IPUnits_Cooling ) {
				PreDefTableEntry( pdchEMcoolannual, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMValue * convertJtoGJ );
				PreDefTableEntry( pdchEMcoolminvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMinVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMcoolminvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMinValDate ) );
				PreDefTableEntry( pdchEMcoolmaxvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMaxVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMcoolmaxvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMaxValDate ) );
			} else if ( SELECT_CASE_var == RT_IPUnits_Water ) {
				PreDefTableEntry( pdchEMwaterannual, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMValue );
				PreDefTableEntry( pdchEMwaterminvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMinVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMwaterminvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMinValDate ) );
				PreDefTableEntry( pdchEMwatermaxvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMaxVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMwatermaxvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMaxValDate ) );
			} else if ( SELECT_CASE_var == RT_IPUnits_OtherKG ) {
				PreDefTableEntry( pdchEMotherKGannual, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMValue );
				PreDefTableEntry( pdchEMotherKGminvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMinVal / SecondsPerTimeStep, 3 );
				PreDefTableEntry( pdchEMotherKGminvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMinValDate ) );
				PreDefTableEntry( pdchEMotherKGmaxvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMaxVal / SecondsPerTimeStep, 3 );
				PreDefTableEntry( pdchEMotherKGmaxvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMaxValDate ) );
			} else if ( SELECT_CASE_var == RT_IPUnits_OtherM3 ) {
				PreDefTableEntry( pdchEMotherM3annual, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMValue, 3 );
				PreDefTableEntry( pdchEMotherM3minvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMinVal / SecondsPerTimeStep, 3 );
				PreDefTableEntry( pdchEMotherM3minvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMinValDate ) );
				PreDefTableEntry( pdchEMotherM3maxvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMaxVal / SecondsPerTimeStep, 3 );
				PreDefTableEntry( pdchEMotherM3maxvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMaxValDate ) );
			} else if ( SELECT_CASE_var == RT_IPUnits_OtherL ) {
				PreDefTableEntry( pdchEMotherLannual, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMValue, 3 );
				PreDefTableEntry( pdchEMotherLminvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMinVal / SecondsPerTimeStep, 3 );
				PreDefTableEntry( pdchEMotherLminvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMinValDate ) );
				PreDefTableEntry( pdchEMotherLmaxvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMaxVal / SecondsPerTimeStep, 3 );
				PreDefTableEntry( pdchEMotherLmaxvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMaxValDate ) );
			} else {
				PreDefTableEntry( pdchEMotherJannual, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMValue * convertJtoGJ );
				PreDefTableEntry( pdchEMotherJminvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMinVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMotherJminvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMinValDate ) );
				PreDefTableEntry( pdchEMotherJmaxvalue, EnergyMeters( Loop ).Name, EnergyMeters( Loop ).LastSMMaxVal / SecondsPerTimeStep );
				PreDefTableEntry( pdchEMotherJmaxvaluetime, EnergyMeters( Loop ).Name, DateToStringWithMonth( EnergyMeters( Loop ).LastSMMaxValDate ) );
			}}
		}

	}

	Fstring
	DateToStringWithMonth( int const codedDate ) // word containing encoded month, day, hour, minute
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR         Jason Glazer
		//       DATE WRITTEN   August 2003
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		//   Convert the coded date format into a usable
		//   string

		// Using/Aliasing
		using General::DecodeMonDayHrMin;

		// Return value
		Fstring StringOut( 12 );

		// Locals
		// ((month*100 + day)*100 + hour)*100 + minute
		static Fstring const DateFmt( "(I2.2,'-',A3,'-',I2.2,':',I2.2)" );

		int Month; // month in integer format (1-12)
		int Day; // day in integer format (1-31)
		int Hour; // hour in integer format (1-24)
		int Minute; // minute in integer format (0:59)
		Fstring monthName( 3 );

		if ( codedDate != 0 ) {
			monthName = "";
			DecodeMonDayHrMin( codedDate, Month, Day, Hour, Minute );
			--Hour;
			if ( Minute == 60 ) {
				++Hour;
				Minute = 0;
			}
			{ auto const SELECT_CASE_var( Month );
			if ( SELECT_CASE_var == 1 ) {
				monthName = "JAN";
			} else if ( SELECT_CASE_var == 2 ) {
				monthName = "FEB";
			} else if ( SELECT_CASE_var == 3 ) {
				monthName = "MAR";
			} else if ( SELECT_CASE_var == 4 ) {
				monthName = "APR";
			} else if ( SELECT_CASE_var == 5 ) {
				monthName = "MAY";
			} else if ( SELECT_CASE_var == 6 ) {
				monthName = "JUN";
			} else if ( SELECT_CASE_var == 7 ) {
				monthName = "JUL";
			} else if ( SELECT_CASE_var == 8 ) {
				monthName = "AUG";
			} else if ( SELECT_CASE_var == 9 ) {
				monthName = "SEP";
			} else if ( SELECT_CASE_var == 10 ) {
				monthName = "OCT";
			} else if ( SELECT_CASE_var == 11 ) {
				monthName = "NOV";
			} else if ( SELECT_CASE_var == 12 ) {
				monthName = "DEC";
			} else {
				monthName = "***";
			}}
			gio::write( StringOut, DateFmt ) << Day << monthName << Hour << Minute;
			if ( index( StringOut, "*" ) > 0 ) {
				StringOut = "-";
			}
		} else { // codeddate = 0
			StringOut = "-";
		}

		return StringOut;
	}

	void
	ReportMeterDetails()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Linda Lawrie
		//       DATE WRITTEN   January 2006
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// Writes the meter details report.  This shows which variables are on
		// meters as well as the meter contents.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int VarMeter;
		int VarMeter1;
		int Meter;
		Fstring MtrUnits( 16 ); // Units for Meter
		int I;
		Fstring String( 32 );
		Fstring Multipliers( 80 );
		int ZoneMult; // Zone Multiplier
		int ZoneListMult; // Zone List Multiplier
		bool CustDecWritten;

		for ( VarMeter = 1; VarMeter <= NumVarMeterArrays; ++VarMeter ) {

			MtrUnits = RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).UnitsString;

			Multipliers = "";
			ZoneMult = RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarPtr().ZoneMult;
			ZoneListMult = RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarPtr().ZoneListMult;

			if ( ZoneMult > 1 || ZoneListMult > 1 ) {
				gio::write( String, "*" ) << ZoneMult * ZoneListMult;
				Multipliers = " * " + adjustl( String );
				gio::write( String, "*" ) << ZoneMult;
				Multipliers = trim( Multipliers ) + "  (Zone Multiplier = " + adjustl( String );
				gio::write( String, "*" ) << ZoneListMult;
				Multipliers = trim( Multipliers ) + ", Zone List Multiplier = " + trim( adjustl( String ) ) + ")";
			}

			gio::write( OutputFileMeterDetails, "(/,A)" ) << " Meters for " + trim( RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarPtr().ReportIDChr ) + "," + trim( RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarName ) + " [" + trim( MtrUnits ) + "]" + trim( Multipliers );

			for ( I = 1; I <= VarMeterArrays( VarMeter ).NumOnMeters; ++I ) {
				gio::write( OutputFileMeterDetails, fmta ) << "  OnMeter=" + trim( EnergyMeters( VarMeterArrays( VarMeter ).OnMeters( I ) ).Name ) + " [" + trim( MtrUnits ) + "]";
			}

			for ( I = 1; I <= VarMeterArrays( VarMeter ).NumOnCustomMeters; ++I ) {
				gio::write( OutputFileMeterDetails, fmta ) << "  OnCustomMeter=" + trim( EnergyMeters( VarMeterArrays( VarMeter ).OnCustomMeters( I ) ).Name ) + " [" + trim( MtrUnits ) + "]";
			}
		}

		for ( Meter = 1; Meter <= NumEnergyMeters; ++Meter ) {
			{ IOFlags flags; flags.ADVANCE( "No" ); gio::write( OutputFileMeterDetails, "(/,A)", flags ) << " For Meter=" + trim( EnergyMeters( Meter ).Name ) + " [" + trim( EnergyMeters( Meter ).Units ) + "]"; }
			if ( EnergyMeters( Meter ).ResourceType != " " ) { IOFlags flags; flags.ADVANCE( "No" ); gio::write( OutputFileMeterDetails, fmta, flags ) << ", ResourceType=" + trim( EnergyMeters( Meter ).ResourceType ); };
			if ( EnergyMeters( Meter ).EndUse != " " ) { IOFlags flags; flags.ADVANCE( "No" ); gio::write( OutputFileMeterDetails, fmta, flags ) << ", EndUse=" + trim( EnergyMeters( Meter ).EndUse ); };
			if ( EnergyMeters( Meter ).Group != " " ) { IOFlags flags; flags.ADVANCE( "No" ); gio::write( OutputFileMeterDetails, fmta, flags ) << ", Group=" + trim( EnergyMeters( Meter ).Group ); };
			gio::write( OutputFileMeterDetails, fmta ) << ", contents are:";

			CustDecWritten = false;

			for ( VarMeter = 1; VarMeter <= NumVarMeterArrays; ++VarMeter ) {
				if ( EnergyMeters( Meter ).TypeOfMeter == MeterType_Normal ) {
					if ( any_eq( VarMeterArrays( VarMeter ).OnMeters, Meter ) ) {
						for ( VarMeter1 = 1; VarMeter1 <= VarMeterArrays( VarMeter ).NumOnMeters; ++VarMeter1 ) {
							if ( VarMeterArrays( VarMeter ).OnMeters( VarMeter1 ) != Meter ) continue;

							Multipliers = "";
							ZoneMult = RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarPtr().ZoneMult;
							ZoneListMult = RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarPtr().ZoneListMult;

							if ( ZoneMult > 1 || ZoneListMult > 1 ) {
								gio::write( String, "*" ) << ZoneMult * ZoneListMult;
								Multipliers = " * " + adjustl( String );
								gio::write( String, "*" ) << ZoneMult;
								Multipliers = trim( Multipliers ) + "  (Zone Multiplier = " + adjustl( String );
								gio::write( String, "*" ) << ZoneListMult;
								Multipliers = trim( Multipliers ) + ", Zone List Multiplier = " + trim( adjustl( String ) ) + ")";
							}

							gio::write( OutputFileMeterDetails, fmta ) << "  " + trim( RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarName ) + trim( Multipliers );
						}
					}
				}
				if ( EnergyMeters( Meter ).TypeOfMeter != MeterType_Normal ) {
					if ( VarMeterArrays( VarMeter ).NumOnCustomMeters > 0 ) {
						if ( any_eq( VarMeterArrays( VarMeter ).OnCustomMeters, Meter ) ) {
							if ( ! CustDecWritten && EnergyMeters( Meter ).TypeOfMeter == MeterType_CustomDec ) {
								gio::write( OutputFileMeterDetails, fmta ) << " Values for this meter will be Source Meter=" + trim( EnergyMeters( EnergyMeters( Meter ).SourceMeter ).Name ) + "; but will be decremented by:";
								CustDecWritten = true;
							}
							for ( VarMeter1 = 1; VarMeter1 <= VarMeterArrays( VarMeter ).NumOnCustomMeters; ++VarMeter1 ) {
								if ( VarMeterArrays( VarMeter ).OnCustomMeters( VarMeter1 ) != Meter ) continue;

								Multipliers = "";
								ZoneMult = RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarPtr().ZoneMult;
								ZoneListMult = RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarPtr().ZoneListMult;

								if ( ZoneMult > 1 || ZoneListMult > 1 ) {
									gio::write( String, "*" ) << ZoneMult * ZoneListMult;
									Multipliers = " * " + adjustl( String );
									gio::write( String, "*" ) << ZoneMult;
									Multipliers = trim( Multipliers ) + "  (Zone Multiplier = " + adjustl( String );
									gio::write( String, "*" ) << ZoneListMult;
									Multipliers = trim( Multipliers ) + ", Zone List Multiplier = " + trim( adjustl( String ) ) + ")";
								}

								gio::write( OutputFileMeterDetails, fmta ) << "  " + trim( RVariableTypes( VarMeterArrays( VarMeter ).RepVariable ).VarName ) + trim( Multipliers );
							}
						}
					}
				}
			}
		}

	}

	// *****************************************************************************
	// End of routines for Energy Meters implementation in EnergyPlus.
	// *****************************************************************************

	void
	AddEndUseSubcategory(
		Fstring const & ResourceName,
		Fstring const & EndUseName,
		Fstring const & EndUseSubName
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Peter Graham Ellis
		//       DATE WRITTEN   February 2006
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine manages the list of subcategories for each end-use category.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::SameString;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		bool Found;
		int EndUseNum;
		int EndUseSubNum;
		int NumSubs;
		FArray1D_Fstring SubcategoryNameTemp( sFstring( MaxNameLength ) );

		Found = false;
		for ( EndUseNum = 1; EndUseNum <= NumEndUses; ++EndUseNum ) {
			if ( SameString( EndUseCategory( EndUseNum ).Name, EndUseName ) ) {

				for ( EndUseSubNum = 1; EndUseSubNum <= EndUseCategory( EndUseNum ).NumSubcategories; ++EndUseSubNum ) {
					if ( SameString( EndUseCategory( EndUseNum ).SubcategoryName( EndUseSubNum ), EndUseSubName ) ) {
						// Subcategory already exists, no further action required
						Found = true;
						break;
					}
				}

				if ( ! Found ) {
					// Add the subcategory by reallocating the array
					NumSubs = EndUseCategory( EndUseNum ).NumSubcategories;
					if ( NumSubs > 0 ) {
						SubcategoryNameTemp.allocate( NumSubs );
						SubcategoryNameTemp = EndUseCategory( EndUseNum ).SubcategoryName;
						EndUseCategory( EndUseNum ).SubcategoryName.deallocate();
					}

					EndUseCategory( EndUseNum ).SubcategoryName.allocate( NumSubs + 1 );
					if ( NumSubs > 0 ) {
						EndUseCategory( EndUseNum ).SubcategoryName( {1,NumSubs} ) = SubcategoryNameTemp( {1,NumSubs} );
						SubcategoryNameTemp.deallocate();
					}

					EndUseCategory( EndUseNum ).NumSubcategories = NumSubs + 1;
					EndUseCategory( EndUseNum ).SubcategoryName( NumSubs + 1 ) = EndUseSubName;

					if ( EndUseCategory( EndUseNum ).NumSubcategories > MaxNumSubcategories ) {
						MaxNumSubcategories = EndUseCategory( EndUseNum ).NumSubcategories;
					}

					Found = true;
				}
				break;
			}
		}

		if ( ! Found ) {
			ShowSevereError( "Nonexistent end use passed to AddEndUseSubcategory=" + trim( EndUseName ) );
		}

	}

	int
	WriteTimeStampFormatData(
		int const unitNumber, // the Fortran output unit number
		int const reportingInterval, // See Module Parameter Definitons for ReportEach, ReportTimeStep, ReportHourly, etc.
		int const reportID, // The ID of the time stamp
		Fstring const & reportIDString, // The ID of the time stamp
		int const DayOfSim, // the number of days simulated so far
		Fstring const & DayOfSimChr, // the number of days simulated so far
		Optional_int_const Month, // the month of the reporting interval
		Optional_int_const DayOfMonth, // The day of the reporting interval
		Optional_int_const Hour, // The hour of the reporting interval
		Optional< Real64 const > EndMinute, // The last minute in the reporting interval
		Optional< Real64 const > StartMinute, // The starting minute of the reporting interval
		Optional_int_const DST, // A flag indicating whether daylight savings time is observed
		Optional_Fstring_const DayType // The day tied for the data (e.g., Monday)
	)
	{

		// FUNCTION INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   July 2008
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS FUNCTION:
		// This function reports the timestamp data for the output processor
		// Much of the code in this function was embedded in earlier versions of EnergyPlus
		// and was moved to this location to simplify maintenance and to allow for data output
		// to the SQL database

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using namespace DataPrecisionGlobals;
		using namespace SQLiteProcedures;

		// Return value
		int WriteTimeStampFormatData;

		// Locals
		// FUNCTION ARGUMENT DEFINITIONS:

		// FUNCTION PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// FUNCTION LOCAL VARIABLE DECLARATIONS:
		Fstring cmessageBuffer( MaxMessageSize );
		int timeIndex;

		{ auto const SELECT_CASE_var( reportingInterval );

		if ( ( SELECT_CASE_var == ReportEach ) || ( SELECT_CASE_var == ReportTimeStep ) ) {
			gio::write( unitNumber, TimeStampFormat ) << trim( reportIDString ) << trim( DayOfSimChr ) << Month << DayOfMonth << DST << Hour << StartMinute << EndMinute << DayType;
			timeIndex = -1; // Signals that writing to the SQL database is not active

			if ( WriteOutputToSQLite ) timeIndex = CreateSQLiteTimeIndexRecord( reportingInterval, reportID, DayOfSim, Month, DayOfMonth, Hour, EndMinute, StartMinute, DST, DayType );

		} else if ( SELECT_CASE_var == ReportHourly ) {
			gio::write( unitNumber, TimeStampFormat ) << trim( reportIDString ) << trim( DayOfSimChr ) << Month << DayOfMonth << DST << Hour << 0.0 << 60.0 << DayType;
			timeIndex = -1; // Signals that writing to the SQL database is not active

			if ( WriteOutputToSQLite ) timeIndex = CreateSQLiteTimeIndexRecord( reportingInterval, reportID, DayOfSim, Month, DayOfMonth, Hour, _, _, DST, DayType );

		} else if ( SELECT_CASE_var == ReportDaily ) {
			gio::write( unitNumber, DailyStampFormat ) << trim( reportIDString ) << trim( DayOfSimChr ) << Month << DayOfMonth << DST << DayType;
			timeIndex = -1; // Signals that writing to the SQL database is not active

			if ( WriteOutputToSQLite ) timeIndex = CreateSQLiteTimeIndexRecord( reportingInterval, reportID, DayOfSim, Month, DayOfMonth, _, _, _, DST, DayType );

		} else if ( SELECT_CASE_var == ReportMonthly ) {
			gio::write( unitNumber, MonthlyStampFormat ) << trim( reportIDString ) << trim( DayOfSimChr ) << Month;
			timeIndex = -1; // Signals that writing to the SQL database is not active
			if ( WriteOutputToSQLite ) timeIndex = CreateSQLiteTimeIndexRecord( ReportMonthly, reportID, DayOfSim, Month );

		} else if ( SELECT_CASE_var == ReportSim ) {
			gio::write( unitNumber, RunPeriodStampFormat ) << trim( reportIDString ) << trim( DayOfSimChr );
			timeIndex = -1; // Signals that writing to the SQL database is not active
			if ( WriteOutputToSQLite ) timeIndex = CreateSQLiteTimeIndexRecord( reportingInterval, reportID, DayOfSim );

		} else {
			gio::write( cmessageBuffer, "(A,I5)" ) << "Illegal reportingInterval passed to WriteTimeStampFormatData: " << reportingInterval;
			if ( WriteOutputToSQLite ) SQLiteWriteMessageMacro( cmessageBuffer );
			timeIndex = -1; // Signals that writing to the SQL database is not active

		}}

		WriteTimeStampFormatData = timeIndex;

		return WriteTimeStampFormatData;

	}

	void
	WriteReportVariableDictionaryItem(
		int const reportingInterval, // The reporting interval (e.g., hourly, daily)
		int const storeType,
		int const reportID, // The reporting ID for the data
		int const indexGroupKey, // The reporting group (e.g., Zone, Plant Loop, etc.)
		Fstring const & indexGroup, // The reporting group (e.g., Zone, Plant Loop, etc.)
		Fstring const & reportIDChr, // The reporting ID for the data
		Fstring const & keyedValue, // The key name for the data
		Fstring const & variableName, // The variable's actual name
		int const indexType,
		Fstring const & UnitsString, // The variables units
		Optional_Fstring_const ScheduleName
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   August 2008
		//       MODIFIED       April 2011; Linda Lawrie
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine writes the ESO data dictionary information to the output files
		// and the SQL database

		// METHODOLOGY EMPLOYED:

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataGlobals::OutputFileStandard;
		using namespace SQLiteProcedures;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring FreqString( 120 );

		FreqString = FreqNotice( reportingInterval, storeType );

		if ( present( ScheduleName ) ) {
			FreqString = trim( FreqString ) + "," + trim( ScheduleName );
		}

		{ auto const SELECT_CASE_var( reportingInterval );

		if ( ( SELECT_CASE_var == ReportEach ) || ( SELECT_CASE_var == ReportTimeStep ) ) {
			gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",1," + trim( keyedValue ) + "," + trim( variableName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );

		} else if ( SELECT_CASE_var == ReportHourly ) {
			TrackingHourlyVariables = true;
			gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",1," + trim( keyedValue ) + "," + trim( variableName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );

		} else if ( SELECT_CASE_var == ReportDaily ) {
			TrackingDailyVariables = true;
			gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",7," + trim( keyedValue ) + "," + trim( variableName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );

		} else if ( SELECT_CASE_var == ReportMonthly ) {
			TrackingMonthlyVariables = true;
			gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",9," + trim( keyedValue ) + "," + trim( variableName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );

		} else if ( SELECT_CASE_var == ReportSim ) {
			TrackingRunPeriodVariables = true;
			gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",11," + trim( keyedValue ) + "," + trim( variableName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );

		}}

		if ( WriteOutputToSQLite ) {
			if ( ! present( ScheduleName ) ) {
				CreateSQLiteReportVariableDictionaryRecord( reportID, storeType, indexGroup, keyedValue, variableName, indexType, UnitsString, reportingInterval );
			} else {
				CreateSQLiteReportVariableDictionaryRecord( reportID, storeType, indexGroup, keyedValue, variableName, indexType, UnitsString, reportingInterval, ScheduleName );
			}
		}

	}

	void
	WriteMeterDictionaryItem(
		int const reportingInterval, // The reporting interval (e.g., hourly, daily)
		int const storeType,
		int const reportID, // The reporting ID in for the variable
		int const indexGroupKey, // The reporting group for the variable
		Fstring const & indexGroup, // The reporting group for the variable
		Fstring const & reportIDChr, // The reporting ID in for the variable
		Fstring const & meterName, // The variable's meter name
		Fstring const & UnitsString, // The variables units
		bool const cumulativeMeterFlag, // A flag indicating cumulative data
		bool const meterFileOnlyFlag // A flag indicating whether the data is to be written to standard output
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   August 2008
		//       MODIFIED       April 2011; Linda Lawrie
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// The subroutine writes meter data dictionary information to the output files
		// and the SQL database. Much of the code here was embedded in other subroutines
		// and was moved here for the purposes of ease of maintenance and to allow easy
		// data reporting to the SQL database

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataGlobals::OutputFileMeters;
		using DataGlobals::OutputFileStandard;
		using namespace SQLiteProcedures;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring FreqString( 120 );
		static Fstring keyedValueString( "Cumulative          " );
		int lenString;

		FreqString = FreqNotice( reportingInterval, storeType );

		{ auto const SELECT_CASE_var( reportingInterval );

		if ( ( SELECT_CASE_var == ReportEach ) || ( SELECT_CASE_var == ReportTimeStep ) || ( SELECT_CASE_var == ReportHourly ) ) { // -1, 0, 1
			if ( ! cumulativeMeterFlag ) {
				gio::write( OutputFileMeters, fmta ) << trim( reportIDChr ) + ",1," + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
			} else {
				gio::write( OutputFileMeters, fmta ) << trim( reportIDChr ) + ",1,Cumulative " + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
			}

			if ( ! meterFileOnlyFlag ) {
				if ( ! cumulativeMeterFlag ) {
					gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",1," + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
				} else {
					gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",1,Cumulative " + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
				}
			}

		} else if ( SELECT_CASE_var == ReportDaily ) { //  2
			if ( ! cumulativeMeterFlag ) {
				gio::write( OutputFileMeters, fmta ) << trim( reportIDChr ) + ",7," + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
			} else {
				lenString = index( FreqString, "[" );
				gio::write( OutputFileMeters, fmta ) << trim( reportIDChr ) + ",1,Cumulative " + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString( {1,lenString - 1} ) );
			}
			if ( ! meterFileOnlyFlag ) {
				if ( ! cumulativeMeterFlag ) {
					gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",7," + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
				} else {
					lenString = index( FreqString, "[" );
					gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",1,Cumulative " + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString( {1,lenString - 1} ) );
				}
			}

		} else if ( SELECT_CASE_var == ReportMonthly ) { //  3
			if ( ! cumulativeMeterFlag ) {
				gio::write( OutputFileMeters, fmta ) << trim( reportIDChr ) + ",9," + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
			} else {
				lenString = index( FreqString, "[" );
				gio::write( OutputFileMeters, fmta ) << trim( reportIDChr ) + ",1,Cumulative " + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString( {1,lenString - 1} ) );
			}
			if ( ! meterFileOnlyFlag ) {
				if ( ! cumulativeMeterFlag ) {
					gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",9," + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
				} else {
					lenString = index( FreqString, "[" );
					gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",1,Cumulative " + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString( {1,lenString - 1} ) );
				}
			}

		} else if ( SELECT_CASE_var == ReportSim ) { //  4
			if ( ! cumulativeMeterFlag ) {
				gio::write( OutputFileMeters, fmta ) << trim( reportIDChr ) + ",11," + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
			} else {
				lenString = index( FreqString, "[" );
				gio::write( OutputFileMeters, fmta ) << trim( reportIDChr ) + ",1,Cumulative " + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString( {1,lenString - 1} ) );
			}
			if ( ! meterFileOnlyFlag ) {
				if ( ! cumulativeMeterFlag ) {
					gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",11," + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString );
				} else {
					lenString = index( FreqString, "[" );
					gio::write( OutputFileStandard, fmta ) << trim( reportIDChr ) + ",1,Cumulative " + trim( meterName ) + " [" + trim( UnitsString ) + "]" + trim( FreqString( {1,lenString - 1} ) );
				}
			}

		}}

		if ( WriteOutputToSQLite ) {
			if ( cumulativeMeterFlag ) {
				keyedValueString = "Cumulative ";
			} else {
				keyedValueString = "";
			}
			CreateSQLiteMeterDictionaryRecord( reportID, storeType, indexGroup, keyedValueString, meterName, 1, UnitsString, reportingInterval );

			if ( ! meterFileOnlyFlag ) {
				CreateSQLiteReportVariableDictionaryRecord( reportID, storeType, indexGroup, keyedValueString, meterName, 1, UnitsString, reportingInterval );
			}

		}

	}

	void
	WriteRealVariableOutput(
		int const reportType, // The report type or interval (e.g., hourly)
		int const timeIndex // An index that points to the timestamp
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   August 2008
		//       MODIFIED       April 2011; Linda Lawrie
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine writes real report variable data to the output file and
		// SQL database. Much of the code here was an included in earlier versions
		// of the UpdateDataandReport subroutine. The code was moved to facilitate
		// easier maintenance and writing of data to the SQL database.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		// na

		if ( RVar().Report && RVar().ReportFreq == reportType && RVar().Stored ) {
			if ( RVar().NumStored > 0.0 ) {
				WriteReportRealData( RVar().ReportID, RVar().ReportIDChr, timeIndex, RVar().StoreValue, RVar().StoreType, RVar().NumStored, RVar().ReportFreq, RVar().MinValue, RVar().minValueDate, RVar().MaxValue, RVar().maxValueDate );
				++StdOutputRecordCount;
			}

			RVar().StoreValue = 0.0;
			RVar().NumStored = 0.0;
			RVar().MinValue = MinSetValue;
			RVar().MaxValue = MaxSetValue;
			RVar().Stored = false;

		}

	}

	void
	WriteReportRealData(
		int const reportID, // The variable's report ID
		Fstring const & creportID, // variable ID in characters
		int const timeIndex, // An index that points to the timestamp
		Real64 const repValue, // The variable's value
		int const storeType, // Averaged or Sum
		Real64 const numOfItemsStored, // The number of items (hours or timesteps) of data stored
		int const reportingInterval, // The variable's reporting interval (e.g., daily)
		Real64 const minValue, // The variable's minimum value during the reporting interval
		int const minValueDate, // The date the minimum value occurred
		Real64 const MaxValue, // The variable's maximum value during the reporting interval
		int const maxValueDate // The date the maximum value occurred
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   July 2008
		//       MODIFIED       April 2011; Linda Lawrie
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine writes the average real data to the output files and
		// SQL database. It supports the WriteRealVariableOutput subroutine.
		// Much of the code here was an included in earlier versions
		// of the UpdateDataandReport subroutine. The code was moved to facilitate
		// easier maintenance and writing of data to the SQL database.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using namespace DataPrecisionGlobals;
		using DataGlobals::OutputFileStandard;
		using General::RemoveTrailingZeros;
		using namespace SQLiteProcedures;

		// Locals
		static Fstring const fmta( "(A)" );

		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring NumberOut( 32 ); // Character for producing "number out"
		Fstring MaxOut( 55 ); // Character for Max out string
		Fstring MinOut( 55 ); // Character for Min out string
		Real64 repVal; // The variable's value

		repVal = repValue;
		if ( storeType == AveragedVar ) repVal /= numOfItemsStored;
		if ( repVal == 0.0 ) {
			NumberOut = "0.0";
		} else {
			gio::write( NumberOut, "*" ) << repVal;
			NumberOut = adjustl( NumberOut );
			NumberOut = RemoveTrailingZeros( NumberOut );
		}

		if ( MaxValue == 0.0 ) {
			MaxOut = "0.0";
		} else {
			gio::write( MaxOut, "*" ) << MaxValue;
			MaxOut = adjustl( MaxOut );
			MaxOut = RemoveTrailingZeros( MaxOut );
		}

		if ( minValue == 0.0 ) {
			MinOut = "0.0";
		} else {
			gio::write( MinOut, "*" ) << minValue;
			MinOut = adjustl( MinOut );
			MinOut = RemoveTrailingZeros( MinOut );
		}

		// Append the min and max strings with date information
		ProduceMinMaxString( MinOut, minValueDate, reportingInterval );
		ProduceMinMaxString( MaxOut, maxValueDate, reportingInterval );

		if ( WriteOutputToSQLite ) {
			CreateSQLiteReportVariableDataRecord( reportID, timeIndex, repVal, reportingInterval, minValue, minValueDate, MaxValue, maxValueDate );
		}

		{ auto const SELECT_CASE_var( reportingInterval );

		if ( ( SELECT_CASE_var == ReportEach ) || ( SELECT_CASE_var == ReportTimeStep ) || ( SELECT_CASE_var == ReportHourly ) ) { // -1, 0, 1
			gio::write( OutputFileStandard, fmta ) << trim( creportID ) + "," + trim( NumberOut );

		} else if ( ( SELECT_CASE_var == ReportDaily ) || ( SELECT_CASE_var == ReportMonthly ) || ( SELECT_CASE_var == ReportSim ) ) { //  2, 3, 4
			gio::write( OutputFileStandard, fmta ) << trim( creportID ) + "," + trim( NumberOut ) + "," + trim( MinOut ) + "," + trim( MaxOut );

		}}

	}

	void
	WriteCumulativeReportMeterData(
		int const reportID, // The variable's report ID
		Fstring const & creportID, // variable ID in characters
		int const timeIndex, // An index that points to the timestamp
		Real64 const repValue, // The variable's value
		bool const meterOnlyFlag // A flag that indicates if the data should be written to standard output
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   July 2008
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine writes the cumulative meter data to the output files and
		// SQL database.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataGlobals::OutputFileMeters;
		using DataGlobals::OutputFileStandard;
		using DataGlobals::StdOutputRecordCount;
		using DataGlobals::StdMeterRecordCount;
		using General::RemoveTrailingZeros;
		using namespace SQLiteProcedures;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring NumberOut( 32 ); // Character for producing "number out"

		if ( repValue == 0.0 ) {
			NumberOut = "0.0";
		} else {
			gio::write( NumberOut, "*" ) << repValue;
			NumberOut = adjustl( NumberOut );
			NumberOut = RemoveTrailingZeros( NumberOut );
		}

		if ( WriteOutputToSQLite ) {
			CreateSQLiteMeterRecord( reportID, timeIndex, repValue );
		}

		gio::write( OutputFileMeters, fmta ) << trim( creportID ) + "," + trim( NumberOut );
		++StdMeterRecordCount;

		if ( ! meterOnlyFlag ) {
			if ( WriteOutputToSQLite ) {
				CreateSQLiteReportVariableDataRecord( reportID, timeIndex, repValue );
			}

			gio::write( OutputFileStandard, fmta ) << trim( creportID ) + "," + trim( NumberOut );
			++StdOutputRecordCount;
		}

	}

	void
	WriteReportMeterData(
		int const reportID, // The variable's report ID
		Fstring const & creportID, // variable ID in characters
		int const timeIndex, // An index that points to the timestamp
		Real64 const repValue, // The variable's value
		int const reportingInterval, // The variable's reporting interval (e.g., hourly)
		Real64 const minValue, // The variable's minimum value during the reporting interval
		int const minValueDate, // The date the minimum value occurred
		Real64 const MaxValue, // The variable's maximum value during the reporting interval
		int const maxValueDate, // The date of the maximum value
		bool const meterOnlyFlag // Indicates whether the data is for the meter file only
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   July 2008
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine writes for the non-cumulative meter data to the output files and
		// SQL database.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using namespace DataPrecisionGlobals;
		using DataGlobals::OutputFileStandard;
		using DataGlobals::OutputFileMeters;
		using DataGlobals::StdOutputRecordCount;
		using DataGlobals::StdMeterRecordCount;
		using General::RemoveTrailingZeros;
		using namespace SQLiteProcedures;

		// Locals
		static Fstring const fmta( "(A)" );

		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring NumberOut( 32 ); // Character for producing "number out"
		Fstring MaxOut( 55 ); // Character for Max out string
		Fstring MinOut( 55 ); // Character for Min out string

		if ( repValue == 0.0 ) {
			NumberOut = "0.0";
		} else {
			gio::write( NumberOut, "*" ) << repValue;
			NumberOut = adjustl( NumberOut );
			NumberOut = RemoveTrailingZeros( NumberOut );
		}

		if ( MaxValue == 0.0 ) {
			MaxOut = "0.0";
		} else {
			gio::write( MaxOut, "*" ) << MaxValue;
			MaxOut = adjustl( MaxOut );
			MaxOut = RemoveTrailingZeros( MaxOut );
		}

		if ( minValue == 0.0 ) {
			MinOut = "0.0";
		} else {
			gio::write( MinOut, "*" ) << minValue;
			MinOut = adjustl( MinOut );
			MinOut = RemoveTrailingZeros( MinOut );
		}

		if ( WriteOutputToSQLite ) {
			CreateSQLiteMeterRecord( reportID, timeIndex, repValue, reportingInterval, minValue, minValueDate, MaxValue, maxValueDate, MinutesPerTimeStep );
		}

		// Append the min and max strings with date information
		//    CALL ProduceMinMaxStringWStartMinute(MinOut, minValueDate, reportingInterval)
		//    CALL ProduceMinMaxStringWStartMinute(MaxOut, maxValueDate, reportingInterval)
		ProduceMinMaxString( MinOut, minValueDate, reportingInterval );
		ProduceMinMaxString( MaxOut, maxValueDate, reportingInterval );

		{ auto const SELECT_CASE_var( reportingInterval );

		if ( ( SELECT_CASE_var == ReportEach ) || ( SELECT_CASE_var == ReportTimeStep ) || ( SELECT_CASE_var == ReportHourly ) ) { // -1, 0, 1
			gio::write( OutputFileMeters, fmta ) << trim( creportID ) + "," + trim( NumberOut );
			++StdMeterRecordCount;

		} else if ( ( SELECT_CASE_var == ReportDaily ) || ( SELECT_CASE_var == ReportMonthly ) || ( SELECT_CASE_var == ReportSim ) ) { //  2, 3, 4
			gio::write( OutputFileMeters, fmta ) << trim( creportID ) + "," + trim( NumberOut ) + "," + trim( MinOut ) + "," + trim( MaxOut );
			++StdMeterRecordCount;

		}}

		if ( ! meterOnlyFlag ) {
			if ( WriteOutputToSQLite ) {
				CreateSQLiteReportVariableDataRecord( reportID, timeIndex, repValue, reportingInterval, minValue, minValueDate, MaxValue, maxValueDate, MinutesPerTimeStep );
			}

			{ auto const SELECT_CASE_var( reportingInterval );

			if ( ( SELECT_CASE_var == ReportEach ) || ( SELECT_CASE_var == ReportTimeStep ) || ( SELECT_CASE_var == ReportHourly ) ) { // -1, 0, 1
				gio::write( OutputFileStandard, fmta ) << trim( creportID ) + "," + trim( NumberOut );
				++StdOutputRecordCount;
			} else if ( ( SELECT_CASE_var == ReportDaily ) || ( SELECT_CASE_var == ReportMonthly ) || ( SELECT_CASE_var == ReportSim ) ) { //  2, 3, 4
				gio::write( OutputFileStandard, fmta ) << trim( creportID ) + "," + trim( NumberOut ) + "," + trim( MinOut ) + "," + trim( MaxOut );
				++StdOutputRecordCount;
			}}

		}

	}

	void
	WriteRealData(
		int const reportID, // The variable's reporting ID
		Fstring const & creportID, // variable ID in characters
		int const timeIndex, // An index that points to the timestamp for the variable
		Real64 const repValue // The variable's value
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   July 2008
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine writes real data to the output files and
		// SQL database. It supports the WriteRealVariableOutput subroutine.
		// Much of the code here was an included in earlier versions
		// of the UpdateDataandReport subroutine. The code was moved to facilitate
		// easier maintenance and writing of data to the SQL database.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataGlobals::OutputFileStandard;
		using General::RemoveTrailingZeros;
		using DataSystemVariables::ReportDuringWarmup;
		using DataSystemVariables::UpdateDataDuringWarmupExternalInterface;
		using namespace SQLiteProcedures;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring NumberOut( 32 ); // Character for producing "number out"

		if ( UpdateDataDuringWarmupExternalInterface && ! ReportDuringWarmup ) return;

		if ( repValue == 0.0 ) {
			NumberOut = "0.0";
		} else {
			gio::write( NumberOut, "*" ) << repValue;
			NumberOut = adjustl( NumberOut );
			NumberOut = RemoveTrailingZeros( NumberOut );
		}

		if ( WriteOutputToSQLite ) {
			CreateSQLiteReportVariableDataRecord( reportID, timeIndex, repValue );
		}

		gio::write( OutputFileStandard, fmta ) << trim( creportID ) + "," + trim( NumberOut );

	}

	void
	WriteIntegerVariableOutput(
		int const reportType, // The report type (i.e., the reporting interval)
		int const timeIndex // An index that points to the timestamp for this data
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   August 2008
		//       MODIFIED       April 2011; Linda Lawrie
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine writes integer report variable data to the output file and
		// SQL database. Much of the code here was an included in earlier versions
		// of the UpdateDataandReport subroutine. The code was moved to facilitate
		// easier maintenance and writing of data to the SQL database.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataSystemVariables::ReportDuringWarmup;
		using DataSystemVariables::UpdateDataDuringWarmupExternalInterface;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		// na

		if ( UpdateDataDuringWarmupExternalInterface && ! ReportDuringWarmup ) return;

		if ( IVar().Report && IVar().ReportFreq == reportType && IVar().Stored ) {
			if ( IVar().NumStored > 0.0 ) {
				WriteReportIntegerData( IVar().ReportID, IVar().ReportIDChr, timeIndex, IVar().StoreValue, IVar().StoreType, IVar().NumStored, IVar().ReportFreq, IVar().MinValue, IVar().minValueDate, IVar().MaxValue, IVar().maxValueDate );
				++StdOutputRecordCount;
			}

			IVar().StoreValue = 0.0;
			IVar().NumStored = 0.0;
			IVar().MinValue = IMinSetValue;
			IVar().MaxValue = IMaxSetValue;
			IVar().Stored = false;

		}

	}

	void
	WriteReportIntegerData(
		int const reportID, // The variable's reporting ID
		Fstring const & reportIDString, // The variable's reporting ID (character)
		int const timeIndex, // An index that points to this timestamp for this data
		Real64 const repValue, // The variable's value
		int const storeType, // Type of item (averaged or summed)
		Optional< Real64 const > numOfItemsStored, // The number of items (hours or timesteps) of data stored //Autodesk:OPTIONAL Used without PRESENT check
		Optional_int_const reportingInterval, // The reporting interval (e.g., monthly) //Autodesk:OPTIONAL Used without PRESENT check
		Optional_int_const minValue, // The variable's minimum value during the reporting interval //Autodesk:OPTIONAL Used without PRESENT check
		Optional_int_const minValueDate, // The date the minimum value occurred //Autodesk:OPTIONAL Used without PRESENT check
		Optional_int_const MaxValue, // The variable's maximum value during the reporting interval //Autodesk:OPTIONAL Used without PRESENT check
		Optional_int_const maxValueDate // The date the maximum value occurred //Autodesk:OPTIONAL Used without PRESENT check
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   July 2008
		//       MODIFIED       April 2011; Linda Lawrie
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine writes averaged integer data to the output files and
		// SQL database. It supports the WriteIntegerVariableOutput subroutine.
		// Much of the code here was an included in earlier versions
		// of the UpdateDataandReport subroutine. The code was moved to facilitate
		// easier maintenance and writing of data to the SQL database.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using namespace DataPrecisionGlobals;
		using DataGlobals::OutputFileStandard;
		using General::RemoveTrailingZeros;
		using namespace SQLiteProcedures;

		// Locals
		static Fstring const fmta( "(A)" );

		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring NumberOut( 32 ); // Character for producing "number out"
		Fstring MaxOut( 55 ); // Character for Max out string
		Fstring MinOut( 55 ); // Character for Min out string
		Real64 rmaxValue;
		Real64 rminValue;
		Real64 repVal; // The variable's value

		repVal = repValue;
		if ( storeType == AveragedVar ) repVal /= numOfItemsStored;
		if ( repValue == 0.0 ) {
			NumberOut = "0.0";
		} else {
			gio::write( NumberOut, "*" ) << repVal;
			NumberOut = adjustl( NumberOut );
			NumberOut = RemoveTrailingZeros( NumberOut );
		}

		// Append the min and max strings with date information
		gio::write( MinOut, "*" ) << minValue;
		gio::write( MaxOut, "*" ) << MaxValue;
		ProduceMinMaxString( MinOut, minValueDate, reportingInterval );
		ProduceMinMaxString( MaxOut, maxValueDate, reportingInterval );

		if ( WriteOutputToSQLite ) {
			rminValue = minValue;
			rmaxValue = MaxValue;
			CreateSQLiteReportVariableDataRecord( reportID, timeIndex, repVal, reportingInterval, rminValue, minValueDate, rmaxValue, maxValueDate );
		}

		{ auto const SELECT_CASE_var( reportingInterval );

		if ( ( SELECT_CASE_var == ReportEach ) || ( SELECT_CASE_var == ReportTimeStep ) || ( SELECT_CASE_var == ReportHourly ) ) { // -1, 0, 1
			gio::write( OutputFileStandard, fmta ) << trim( reportIDString ) + "," + trim( NumberOut );

		} else if ( ( SELECT_CASE_var == ReportDaily ) || ( SELECT_CASE_var == ReportMonthly ) || ( SELECT_CASE_var == ReportSim ) ) { //  2, 3, 4
			gio::write( OutputFileStandard, fmta ) << trim( reportIDString ) + "," + trim( NumberOut ) + "," + trim( MinOut ) + "," + trim( MaxOut );

		}}

	}

	void
	WriteIntegerData(
		int const reportID, // the reporting ID of the data
		Fstring const & reportIDString, // the reporting ID of the data (character)
		int const timeIndex, // an index that points to the data's timestamp
		Optional_int_const IntegerValue, // the value of the data
		Optional< Real64 const > RealValue // the value of the data
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   July 2008
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine writes integer data to the output files and
		// SQL database. It supports the WriteIntegerVariableOutput subroutine.
		// Much of the code here was an included in earlier versions
		// of the UpdateDataandReport subroutine. The code was moved to facilitate
		// easier maintenance and writing of data to the SQL database.

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataGlobals::OutputFileStandard;
		using General::RemoveTrailingZeros;
		using namespace SQLiteProcedures;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Fstring NumberOut( 32 ); // Character for producing "number out"
		Real64 repValue; // for SQLite

		if ( present( IntegerValue ) ) {
			gio::write( NumberOut, "*" ) << IntegerValue;
			NumberOut = adjustl( NumberOut );
			repValue = IntegerValue;
		}
		if ( present( RealValue ) ) {
			repValue = RealValue;
			if ( RealValue == 0.0 ) {
				NumberOut = "0.0";
			} else {
				gio::write( NumberOut, "*" ) << RealValue;
				NumberOut = adjustl( NumberOut );
				NumberOut = RemoveTrailingZeros( NumberOut );
			}
		}

		if ( WriteOutputToSQLite ) {
			CreateSQLiteReportVariableDataRecord( reportID, timeIndex, repValue );
		}

		gio::write( OutputFileStandard, fmta ) << trim( reportIDString ) + "," + trim( NumberOut );

	}

	int
	DetermineIndexGroupKeyFromMeterName( Fstring const & meterName ) // the meter name
	{

		// FUNCTION INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   May 2009
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS FUNCTION:
		// This function attemps to guess determine how a meter variable should be
		// grouped.  It does this by parsing the meter name and then assigns a
		// indexGroupKey based on the name

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Return value
		int DetermineIndexGroupKeyFromMeterName;

		// Locals
		// FUNCTION ARGUMENT DEFINITIONS:

		// FUNCTION LOCAL VARIABLE DECLARATIONS:
		static int indexGroupKey( -1 );

		// Facility indices are in the 100s
		if ( index( meterName, "Electricity:Facility" ) > 0 ) {
			indexGroupKey = 100;
		} else if ( index( meterName, "Gas:Facility" ) > 0 ) {
			indexGroupKey = 101;
		} else if ( index( meterName, "DistricHeating:Facility" ) > 0 ) {
			indexGroupKey = 102;
		} else if ( index( meterName, "DistricCooling:Facility" ) > 0 ) {
			indexGroupKey = 103;
		} else if ( index( meterName, "ElectricityNet:Facility" ) > 0 ) {
			indexGroupKey = 104;

			// Building indices are in the 200s
		} else if ( index( meterName, "Electricity:Building" ) > 0 ) {
			indexGroupKey = 201;
		} else if ( index( meterName, "Gas:Building" ) > 0 ) {
			indexGroupKey = 202;

			// HVAC indices are in the 300s
		} else if ( index( meterName, "Electricity:HVAC" ) > 0 ) {
			indexGroupKey = 301;

			// InteriorLights:Electricity indices are in the 400s
		} else if ( index( meterName, "InteriorLights:Electricity" ) > 0 ) {
			indexGroupKey = 401;

			// InteriorLights:Electricity:Zone indices are in the 500s
		} else if ( index( meterName, "InteriorLights:Electricity:Zone" ) > 0 ) {
			indexGroupKey = 501;

			// Unknown items have negative indices
		} else {
			indexGroupKey = -11;
		}

		DetermineIndexGroupKeyFromMeterName = indexGroupKey;

		return DetermineIndexGroupKeyFromMeterName;

	}

	Fstring
	DetermineIndexGroupFromMeterGroup( MeterType const & meter ) // the meter
	{

		// FUNCTION INFORMATION:
		//       AUTHOR         Greg Stark
		//       DATE WRITTEN   May 2009
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS FUNCTION:
		// This function attemps to determine how a meter variable should be
		// grouped.  It does this by parsing the meter group

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Return value
		Fstring indexGroup( MaxNameLength );

		// Locals
		// FUNCTION ARGUMENT DEFINITIONS:

		// FUNCTION LOCAL VARIABLE DECLARATIONS:

		if ( len( trim( meter.Group ) ) > 0 ) {
			indexGroup = trim( meter.Group );
		} else {
			indexGroup = "Facility";
		}

		if ( len( trim( meter.ResourceType ) ) > 0 ) {
			indexGroup = trim( indexGroup ) + ":" + trim( meter.ResourceType );
		}

		if ( len( trim( meter.EndUse ) ) > 0 ) {
			indexGroup = trim( indexGroup ) + ":" + trim( meter.EndUse );
		}

		if ( len( trim( meter.EndUseSub ) ) > 0 ) {
			indexGroup = trim( indexGroup ) + ":" + trim( meter.EndUseSub );
		}

		return indexGroup;

	}

	void
	SetInternalVariableValue(
		int const varType, // 1=integer, 2=real, 3=meter
		int const keyVarIndex, // Array index
		Real64 const SetRealVal, // real value to set, if type is real or meter
		int const SetIntVal // integer value to set if type is integer
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         B. Griffith
		//       DATE WRITTEN   August 2012
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This is a simple set routine for output pointers
		// It is intended for special use to reinitializations those pointers used for EMS sensors

		// METHODOLOGY EMPLOYED:
		// given a variable type and variable index,
		// assign the pointers the values passed in.

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		{ auto const SELECT_CASE_var( varType );

		if ( SELECT_CASE_var == 1 ) { // Integer
			IVar >>= IVariableTypes( keyVarIndex ).VarPtr;
			IVar().Which() = SetIntVal;
		} else if ( SELECT_CASE_var == 2 ) { // real
			RVar >>= RVariableTypes( keyVarIndex ).VarPtr;
			RVar().Which() = SetRealVal;
		} else if ( SELECT_CASE_var == 3 ) { // meter
			EnergyMeters( keyVarIndex ).CurTSValue = SetRealVal;
		}}

	}

} // OutputProcessor

//==============================================================================================
// *****************************************************************************
// These routines are available outside the OutputProcessor Module (i.e. calling
// routines do not have to "USE OutputProcessor".  But each of these routines
// will use the OutputProcessor and take advantage that everything is PUBLIC
// within the OutputProcessor.
// *****************************************************************************

void
SetupOutputVariable(
	Fstring const & VariableName, // String Name of variable (with units)
	Real64 & ActualVariable, // Actual Variable, used to set up pointer
	Fstring const & IndexTypeKey, // Zone, HeatBalance=1, HVAC, System, Plant=2
	Fstring const & VariableTypeKey, // State, Average=1, NonState, Sum=2
	Fstring const & KeyedValue, // Associated Key for this variable
	Optional_Fstring_const ReportFreq, // Internal use -- causes reporting at this freqency
	Optional_Fstring_const ResourceTypeKey, // Meter Resource Type (Electricity, Gas, etc)
	Optional_Fstring_const EndUseKey, // Meter End Use Key (Lights, Heating, Cooling, etc)
	Optional_Fstring_const EndUseSubKey, // Meter End Use Sub Key (General Lights, Task Lights, etc)
	Optional_Fstring_const GroupKey, // Meter Super Group Key (Building, System, Plant)
	Optional_Fstring_const ZoneKey, // Meter Zone Key (zone name)
	Optional_int_const ZoneMult, // Zone Multiplier, defaults to 1
	Optional_int_const ZoneListMult, // Zone List Multiplier, defaults to 1
	Optional_int_const indexGroupKey // Group identifier for SQL output
)
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda K. Lawrie
	//       DATE WRITTEN   December 1998
	//       MODIFIED       January 2001; Implement Meters
	//                      August 2008; Implement SQL output
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine sets up the variable data structure that will be used
	// to track values of the output variables of EnergyPlus.

	// METHODOLOGY EMPLOYED:
	// Pointers (as pointers), pointers (as indices), and lots of other KEWL data stuff.

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;
	using DataOutputs::FindItemInVariableList;
	using InputProcessor::FindItem;
	using InputProcessor::MakeUPPERCase;
	using InputProcessor::SameString;
	using General::TrimSigDigits;
	using namespace SQLiteProcedures;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	int CV;
	Fstring IDOut( 20 );
	int IndexType; // 1=TimeStepZone, 2=TimeStepSys
	int VariableType; // 1=Average, 2=Sum, 3=Min/Max
	int Loop;
	int RepFreq;
	bool OnMeter; // True if this variable is on a meter
	Fstring VarName( MaxNameLength ); // Variable name without units
	//  CHARACTER(len=MaxNameLength) :: VariableNamewithUnits ! Variable name with units std format
	Fstring ResourceType( MaxNameLength ); // Will hold value of ResourceTypeKey
	Fstring EndUse( MaxNameLength ); // Will hold value of EndUseKey
	Fstring EndUseSub( MaxNameLength ); // Will hold value of EndUseSubKey
	Fstring Group( MaxNameLength ); // Will hold value of GroupKey
	Fstring ZoneName( MaxNameLength ); // Will hold value of ZoneKey
	static bool ErrorsFound( false ); // True if Errors Found
	int Item;
	Fstring MtrUnits( 16 ); // Units for Meter
	bool ThisOneOnTheList;
	static Fstring UnitsString( UnitsStringLength, BlankString ); // Units for Variable (no brackets)
	int localIndexGroupKey;
	bool invalidUnits;

	if ( ! OutputInitialized ) InitializeOutput();

	//! Errors are severe and fatal because should only be encountered during development.
	Item = index( VariableName, "[" );
	if ( Item != 0 ) {
		UnitsString = GetVariableUnitsString( VariableName );
		UnitsString = adjustl( UnitsString );
		VarName = adjustl( VariableName( 1, Item - 1 ) );
		//    VariableNamewithUnits=TRIM(VarName)//' ['//TRIM(UnitsString)//']'
		// Check name length for variable name
		invalidUnits = false;
		if ( UnitsString( 1, 1 ) == "-" ) invalidUnits = true;
		if ( SameString( UnitsString, "dimensionless" ) ) invalidUnits = true;
		if ( len_trim( adjustl( VariableName ) ) > MaxNameLength ) {
			ShowSevereError( "Variable Name length (including units) [" + trim( TrimSigDigits( len_trim( adjustl( VariableName ) ) ) ) + "] exceeds maximum=" + trim( VariableName ) );
			if ( invalidUnits ) ShowSevereError( "Variable has invalid units in call Variable=" + trim( VariableName ) + ", Units=" + trim( UnitsString ) );
			ShowFatalError( "Program terminates." );
		}
		if ( invalidUnits ) {
			ShowSevereError( "Variable has invalid units in call Variable=" + trim( VariableName ) + ", Units=" + trim( UnitsString ) );
			ShowFatalError( "Program terminates." );
		}
	} else { // no units
		UnitsString = BlankString;
		VarName = adjustl( VariableName );
		//    VariableNamewithUnits=TRIM(VarName)//' ['//TRIM(UnitsString)//']'
		if ( len_trim( adjustl( VariableName ) ) > MaxNameLength ) {
			ShowSevereError( "Variable Name has no units in call=" + trim( VariableName ) );
			ShowSevereError( "Variable Name length exceeds maximum=" + trim( VariableName ) );
			ShowFatalError( "Program terminates." );
		}
		ShowSevereError( "Variable Name has no units in call=" + trim( VariableName ) );
		ShowFatalError( "Program terminates." );
	}

	// Determine whether to Report or not
	CheckReportVariable( KeyedValue, VarName );

	if ( NumExtraVars == 0 ) {
		NumExtraVars = 1;
		ReportList = -1;
	}

	// If ReportFreq present, overrides input
	if ( present( ReportFreq ) ) {
		DetermineFrequency( ReportFreq, RepFreq );
		NumExtraVars = 1;
		ReportList = 0;
	}

	ThisOneOnTheList = FindItemInVariableList( KeyedValue, VarName );
	OnMeter = false; // just a safety initialization

	for ( Loop = 1; Loop <= NumExtraVars; ++Loop ) {

		if ( Loop == 1 ) ++NumOfRVariable_Setup;

		if ( Loop == 1 ) {
			OnMeter = false;
			if ( present( ResourceTypeKey ) ) {
				ResourceType = ResourceTypeKey;
				OnMeter = true;
			} else {
				ResourceType = " ";
			}
			if ( present( EndUseKey ) ) {
				EndUse = EndUseKey;
				OnMeter = true;
			} else {
				EndUse = " ";
			}
			if ( present( EndUseSubKey ) ) {
				EndUseSub = EndUseSubKey;
				OnMeter = true;
			} else {
				EndUseSub = " ";
			}
			if ( present( GroupKey ) ) {
				Group = GroupKey;
				OnMeter = true;
			} else {
				Group = " ";
			}
			if ( present( ZoneKey ) ) {
				ZoneName = ZoneKey;
				OnMeter = true;
			} else {
				ZoneName = " ";
			}
		}

		IndexType = ValidateIndexType( IndexTypeKey, "SetupOutputVariable" );
		VariableType = ValidateVariableType( VariableTypeKey );

		AddToOutputVariableList( VarName, IndexType, VariableType, VarType_Real, UnitsString );
		++NumTotalRVariable;

		if ( ! OnMeter && ! ThisOneOnTheList ) continue;

		++NumOfRVariable;
		if ( Loop == 1 && VariableType == SummedVar ) {
			++NumOfRVariable_Sum;
			if ( present( ResourceTypeKey ) ) {
				if ( ResourceTypeKey != BlankString ) ++NumOfRVariable_Meter;
			}
		}
		if ( NumOfRVariable > MaxRVariable ) {
			ReallocateRVar();
		}
		CV = NumOfRVariable;
		RVariableTypes( CV ).IndexType = IndexType;
		RVariableTypes( CV ).StoreType = VariableType;
		RVariableTypes( CV ).VarName = trim( KeyedValue ) + ":" + trim( VarName );
		RVariableTypes( CV ).VarNameOnly = trim( VarName );
		RVariableTypes( CV ).VarNameOnlyUC = MakeUPPERCase( VarName );
		RVariableTypes( CV ).VarNameUC = MakeUPPERCase( RVariableTypes( CV ).VarName );
		RVariableTypes( CV ).KeyNameOnlyUC = MakeUPPERCase( KeyedValue );
		RVariableTypes( CV ).UnitsString = UnitsString;
		AssignReportNumber( CurrentReportNumber );
		gio::write( IDOut, "*" ) << CurrentReportNumber;
		IDOut = adjustl( IDOut );

		RVariable.allocate();
		RVariable().Value = 0.0;
		RVariable().TSValue = 0.0;
		RVariable().StoreValue = 0.0;
		RVariable().NumStored = 0.0;
		RVariable().MaxValue = MaxSetValue;
		RVariable().maxValueDate = 0;
		RVariable().MinValue = MinSetValue;
		RVariable().minValueDate = 0;

		RVariableTypes( CV ).VarPtr >>= RVariable;
		RVariable().Which >>= ActualVariable;
		RVariable().ReportID = CurrentReportNumber;
		RVariableTypes( CV ).ReportID = CurrentReportNumber;
		RVariable().ReportIDChr = IDOut( 1, 15 );
		RVariable().StoreType = VariableType;
		RVariable().Stored = false;
		RVariable().Report = false;
		RVariable().ReportFreq = ReportHourly;
		RVariable().SchedPtr = 0;
		RVariable().MeterArrayPtr = 0;
		RVariable().ZoneMult = 1;
		RVariable().ZoneListMult = 1;
		if ( present( ZoneMult ) && present( ZoneListMult ) ) {
			RVariable().ZoneMult = ZoneMult;
			RVariable().ZoneListMult = ZoneListMult;
		}

		if ( Loop == 1 ) {
			if ( OnMeter ) {
				if ( VariableType == AveragedVar ) {
					ShowSevereError( "Meters can only be \"Summed\" variables" );
					ShowContinueError( "..reference variable=" + trim( KeyedValue ) + ":" + trim( VariableName ) );
					ErrorsFound = true;
				} else {
					MtrUnits = RVariableTypes( CV ).UnitsString;
					ErrorsFound = false;
					AttachMeters( MtrUnits, ResourceType, EndUse, EndUseSub, Group, ZoneName, CV, RVariable().MeterArrayPtr, ErrorsFound );
					if ( ErrorsFound ) {
						ShowContinueError( "Invalid Meter spec for variable=" + trim( KeyedValue ) + ":" + trim( VariableName ) );
						ErrorsLogged = true;
					}
				}
			}
		}

		if ( ReportList( Loop ) == -1 ) continue;

		RVariable().Report = true;

		if ( ReportList( Loop ) == 0 ) {
			RVariable().ReportFreq = RepFreq;
			RVariable().SchedPtr = 0;
		} else {
			RVariable().ReportFreq = ReqRepVars( ReportList( Loop ) ).ReportFreq;
			RVariable().SchedPtr = ReqRepVars( ReportList( Loop ) ).SchedPtr;
		}

		if ( RVariable().Report ) {
			if ( present( indexGroupKey ) ) {
				localIndexGroupKey = indexGroupKey;
			} else {
				localIndexGroupKey = -999; // Unknown Group
			}

			if ( RVariable().SchedPtr != 0 ) {
				WriteReportVariableDictionaryItem( RVariable().ReportFreq, RVariable().StoreType, RVariable().ReportID, localIndexGroupKey, IndexTypeKey, RVariable().ReportIDChr, KeyedValue, VarName, RVariableTypes( CV ).IndexType, RVariableTypes( CV ).UnitsString, ReqRepVars( ReportList( Loop ) ).SchedName );
			} else {
				WriteReportVariableDictionaryItem( RVariable().ReportFreq, RVariable().StoreType, RVariable().ReportID, localIndexGroupKey, IndexTypeKey, RVariable().ReportIDChr, KeyedValue, VarName, RVariableTypes( CV ).IndexType, RVariableTypes( CV ).UnitsString );
			}
		}
	}

}

void
SetupOutputVariable(
	Fstring const & VariableName, // String Name of variable
	int & ActualVariable, // Actual Variable, used to set up pointer
	Fstring const & IndexTypeKey, // Zone, HeatBalance=1, HVAC, System, Plant=2
	Fstring const & VariableTypeKey, // State, Average=1, NonState, Sum=2
	Fstring const & KeyedValue, // Associated Key for this variable
	Optional_Fstring_const ReportFreq, // Internal use -- causes reporting at this freqency
	Optional_int_const indexGroupKey // Group identifier for SQL output
)
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda K. Lawrie
	//       DATE WRITTEN   December 1998
	//       MODIFIED       August 2008; Added SQL output capability
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine sets up the variable data structure that will be used
	// to track values of the output variables of EnergyPlus.

	// METHODOLOGY EMPLOYED:
	// Pointers (as pointers), pointers (as indices), and lots of other KEWL data stuff.

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;
	using InputProcessor::FindItem;
	using InputProcessor::MakeUPPERCase;
	using InputProcessor::SameString;
	using General::TrimSigDigits;
	using DataOutputs::FindItemInVariableList;
	using namespace SQLiteProcedures;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	int CV;
	int Item;
	Fstring IDOut( 20 );
	Fstring VarName( MaxNameLength ); // Variable without units
	//  CHARACTER(len=MaxNameLength) :: VariableNamewithUnits ! Variable name with units std format
	int IndexType; // 1=TimeStepZone, 2=TimeStepSys
	int VariableType; // 1=Average, 2=Sum, 3=Min/Max
	int localIndexGroupKey;
	bool ThisOneOnTheList;
	bool invalidUnits;
	static Fstring UnitsString( UnitsStringLength, BlankString ); // Units for Variable (no brackets)
	int Loop;
	int RepFreq;

	if ( ! OutputInitialized ) InitializeOutput();

	//! Errors are severe and fatal because should only be encountered during development.
	Item = index( VariableName, "[" );
	if ( Item != 0 ) {
		UnitsString = GetVariableUnitsString( VariableName );
		UnitsString = adjustl( UnitsString );
		invalidUnits = false;
		if ( UnitsString( 1, 1 ) == "-" ) invalidUnits = true;
		if ( SameString( UnitsString, "dimensionless" ) ) invalidUnits = true;
		VarName = adjustl( VariableName( 1, Item - 1 ) );
		//    VariableNamewithUnits=TRIM(VarName)//' ['//TRIM(UnitsString)//']'
		// Check name length for variable name
		if ( len_trim( adjustl( VariableName ) ) > MaxNameLength ) {
			ShowSevereError( "Variable Name length (including units) [" + trim( TrimSigDigits( len_trim( adjustl( VariableName ) ) ) ) + "] exceeds maximum=" + trim( VariableName ) );
			if ( invalidUnits ) ShowSevereError( "Variable has invalid units in call Variable=" + trim( VariableName ) + ", Units=" + trim( UnitsString ) );
			ShowFatalError( "Program terminates." );
		}
		if ( invalidUnits ) {
			ShowSevereError( "Variable has invalid units in call Variable=" + trim( VariableName ) + ", Units=" + trim( UnitsString ) );
			ShowFatalError( "Program terminates." );
		}
	} else {
		UnitsString = BlankString;
		VarName = adjustl( VariableName );
		//    VariableNamewithUnits=TRIM(VarName)//' ['//TRIM(UnitsString)//']'
		if ( len_trim( adjustl( VariableName ) ) > MaxNameLength ) {
			ShowSevereError( "Variable Name has no units in call=" + trim( VariableName ) );
			ShowSevereError( "Variable Name length exceeds maximum=" + trim( VariableName ) );
			ShowFatalError( "Program terminates." );
		}
		ShowSevereError( "Variable Name has no units in call=" + trim( VariableName ) );
		ShowFatalError( "Program terminates." );
	}

	// Determine whether to Report or not
	CheckReportVariable( KeyedValue, VarName );

	if ( NumExtraVars == 0 ) {
		NumExtraVars = 1;
		ReportList = -1;
	}

	// If ReportFreq present, overrides input
	if ( present( ReportFreq ) ) {
		DetermineFrequency( ReportFreq, RepFreq );
		NumExtraVars = 1;
		ReportList = 0;
	} else {
		RepFreq = ReportHourly;
	}

	ThisOneOnTheList = FindItemInVariableList( KeyedValue, VarName );

	for ( Loop = 1; Loop <= NumExtraVars; ++Loop ) {

		if ( Loop == 1 ) ++NumOfIVariable_Setup;

		IndexType = ValidateIndexType( IndexTypeKey, "SetupOutputVariable" );
		VariableType = ValidateVariableType( VariableTypeKey );

		AddToOutputVariableList( VarName, IndexType, VariableType, VarType_Integer, UnitsString );
		++NumTotalIVariable;

		if ( ! ThisOneOnTheList ) continue;

		++NumOfIVariable;
		if ( Loop == 1 && VariableType == SummedVar ) {
			++NumOfIVariable_Sum;
		}
		if ( NumOfIVariable > MaxIVariable ) {
			ReallocateIVar();
		}

		CV = NumOfIVariable;
		IVariableTypes( CV ).IndexType = IndexType;
		IVariableTypes( CV ).StoreType = VariableType;
		IVariableTypes( CV ).VarName = trim( KeyedValue ) + ":" + trim( VarName );
		IVariableTypes( CV ).VarNameOnly = trim( VarName );
		IVariableTypes( CV ).VarNameUC = MakeUPPERCase( IVariableTypes( CV ).VarName );
		IVariableTypes( CV ).UnitsString = UnitsString;
		AssignReportNumber( CurrentReportNumber );
		gio::write( IDOut, "*" ) << CurrentReportNumber;
		IDOut = adjustl( IDOut );

		IVariable.allocate();
		IVariable().Value = 0.0;
		IVariable().StoreValue = 0.0;
		IVariable().TSValue = 0.0;
		IVariable().NumStored = 0.0;
		//    IVariable%LastTSValue=0
		IVariable().MaxValue = IMaxSetValue;
		IVariable().maxValueDate = 0;
		IVariable().MinValue = IMinSetValue;
		IVariable().minValueDate = 0;

		IVariableTypes( CV ).VarPtr >>= IVariable;
		IVariable().Which >>= ActualVariable;
		IVariable().ReportID = CurrentReportNumber;
		IVariableTypes( CV ).ReportID = CurrentReportNumber;
		IVariable().ReportIDChr = IDOut( 1, 15 );
		IVariable().StoreType = VariableType;
		IVariable().Stored = false;
		IVariable().Report = false;
		IVariable().ReportFreq = ReportHourly;
		IVariable().SchedPtr = 0;

		if ( ReportList( Loop ) == -1 ) continue;

		IVariable().Report = true;

		if ( ReportList( Loop ) == 0 ) {
			IVariable().ReportFreq = RepFreq;
			IVariable().SchedPtr = 0;
		} else {
			IVariable().ReportFreq = ReqRepVars( ReportList( Loop ) ).ReportFreq;
			IVariable().SchedPtr = ReqRepVars( ReportList( Loop ) ).SchedPtr;
		}

		if ( IVariable().Report ) {
			if ( present( indexGroupKey ) ) {
				localIndexGroupKey = indexGroupKey;
			} else {
				localIndexGroupKey = -999; // Unknown Group
			}

			if ( IVariable().SchedPtr != 0 ) {
				WriteReportVariableDictionaryItem( IVariable().ReportFreq, IVariable().StoreType, IVariable().ReportID, localIndexGroupKey, IndexTypeKey, IVariable().ReportIDChr, KeyedValue, VarName, IVariableTypes( CV ).IndexType, IVariableTypes( CV ).UnitsString, ReqRepVars( ReportList( Loop ) ).SchedName );
			} else {
				WriteReportVariableDictionaryItem( IVariable().ReportFreq, IVariable().StoreType, IVariable().ReportID, localIndexGroupKey, IndexTypeKey, IVariable().ReportIDChr, KeyedValue, VarName, IVariableTypes( CV ).IndexType, IVariableTypes( CV ).UnitsString );
			}
		}
	}

}

void
SetupOutputVariable(
	Fstring const & VariableName, // String Name of variable
	Real64 & ActualVariable, // Actual Variable, used to set up pointer
	Fstring const & IndexTypeKey, // Zone, HeatBalance=1, HVAC, System, Plant=2
	Fstring const & VariableTypeKey, // State, Average=1, NonState, Sum=2
	int const KeyedValue, // Associated Key for this variable
	Optional_Fstring_const ReportFreq, // Internal use -- causes reporting at this freqency
	Optional_Fstring_const ResourceTypeKey, // Meter Resource Type (Electricity, Gas, etc)
	Optional_Fstring_const EndUseKey, // Meter End Use Key (Lights, Heating, Cooling, etc)
	Optional_Fstring_const EndUseSubKey, // Meter End Use Sub Key (General Lights, Task Lights, etc)
	Optional_Fstring_const GroupKey, // Meter Super Group Key (Building, System, Plant)
	Optional_Fstring_const ZoneKey, // Meter Zone Key (zone name)
	Optional_int_const ZoneMult, // Zone Multiplier, defaults to 1
	Optional_int_const ZoneListMult, // Zone List Multiplier, defaults to 1
	Optional_int_const indexGroupKey // Group identifier for SQL output
)
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda K. Lawrie
	//       DATE WRITTEN   February 1999
	//       MODIFIED       January 2001; Implement Meters
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine allows an integer key for a variable.  Changes this to a
	// standard character variable and passes everything to SetupOutputVariable.

	// METHODOLOGY EMPLOYED:
	// Pointers (as pointers), pointers (as indices), and lots of other KEWL data stuff.

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	Fstring IDOut( 20 );

	// Not checking for valid number

	gio::write( IDOut, "*" ) << KeyedValue;
	IDOut = adjustl( IDOut );

	SetupOutputVariable( VariableName, ActualVariable, IndexTypeKey, VariableTypeKey, IDOut, ReportFreq, ResourceTypeKey, EndUseKey, EndUseSubKey, GroupKey, ZoneKey, ZoneMult, ZoneListMult, indexGroupKey );

}

void
UpdateDataandReport( int const IndexTypeKey ) // What kind of data to update (Zone, HVAC)
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda K. Lawrie
	//       DATE WRITTEN   December 1998
	//       MODIFIED       January 2001; Resolution integrated at the Zone TimeStep intervals
	//       MODIFIED       August 2008; Added SQL output capability
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine writes the actual report variable (for user requested
	// Report Variables) strings to the standard output file.

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;
	using ScheduleManager::GetCurrentScheduleValue;
	using DataGlobals::HourOfDay;
	using DataGlobals::DayOfSimChr;
	using DataGlobals::EndHourFlag;
	using DataGlobals::EndDayFlag;
	using DataGlobals::EndEnvrnFlag;
	using DataEnvironment::EndMonthFlag;
	using General::RemoveTrailingZeros;
	using General::EncodeMonDayHrMin;
	using namespace SQLiteProcedures;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	int Loop; // Loop Variable
	int IndexType; // Translate Zone=>1, HVAC=>2
	Real64 CurVal; // Current value for real variables
	Real64 ICurVal; // Current value for integer variables
	int MDHM; // Month,Day,Hour,Minute
	bool TimePrint; // True if the time needs to be printed
	Real64 StartMinute; // StartMinute for UpdateData call
	Real64 MinuteNow; // What minute it is now
	bool ReportNow; // True if this variable should be reported now
	int CurDayType; // What kind of day it is (weekday (sunday, etc) or holiday)
	static int LHourP( -1 ); // Helps set hours for timestamp output
	static Real64 LStartMin( -1.0 ); // Helps set minutes for timestamp output
	static Real64 LEndMin( -1.0 ); // Helps set minutes for timestamp output
	static bool EndTimeStepFlag( false ); // True when it's the end of the Zone Time Step
	Real64 rxTime; // (MinuteNow-StartMinute)/REAL(MinutesPerTimeStep,r64) - for execution time

	IndexType = IndexTypeKey;
	if ( IndexType != ZoneTSReporting && IndexType != HVACTSReporting ) {
		ShowFatalError( "Invalid reporting requested -- UpdateDataAndReport" );
	}

	{ auto const SELECT_CASE_var( IndexType );

	if ( ( SELECT_CASE_var >= ZoneVar ) && ( SELECT_CASE_var <= HVACVar ) ) {

		// Basic record keeping and report out if "detailed"

		StartMinute = TimeValue( IndexType ).CurMinute;
		TimeValue( IndexType ).CurMinute += TimeValue( IndexType ).TimeStep * 60.0;
		if ( IndexType == HVACVar && TimeValue( HVACVar ).CurMinute == TimeValue( ZoneVar ).CurMinute ) {
			EndTimeStepFlag = true;
		} else if ( IndexType == ZoneVar ) {
			EndTimeStepFlag = true;
		} else {
			EndTimeStepFlag = false;
		}
		MinuteNow = TimeValue( IndexType ).CurMinute;

		EncodeMonDayHrMin( MDHM, Month, DayOfMonth, HourOfDay, int( MinuteNow ) );
		TimePrint = true;

		rxTime = ( MinuteNow - StartMinute ) / double( MinutesPerTimeStep );

		// Main "Record Keeping" Loops for R and I variables
		for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
			if ( RVariableTypes( Loop ).IndexType != IndexType ) continue;

			// Act on the RVariables variable using the RVar structure
			RVar >>= RVariableTypes( Loop ).VarPtr;
			RVar().Stored = true;
			if ( RVar().StoreType == AveragedVar ) {
				CurVal = RVar().Which * rxTime;
				//        CALL SetMinMax(RVar%Which,MDHM,RVar%MaxValue,RVar%maxValueDate,RVar%MinValue,RVar%minValueDate)
				if ( RVar().Which > RVar().MaxValue ) {
					RVar().MaxValue = RVar().Which;
					RVar().maxValueDate = MDHM;
				}
				if ( RVar().Which < RVar().MinValue ) {
					RVar().MinValue = RVar().Which;
					RVar().minValueDate = MDHM;
				}
				RVar().TSValue += CurVal;
				RVar().EITSValue = RVar().TSValue; //CR - 8481 fix - 09/06/2011
			} else {
				//        CurVal=RVar%Which
				if ( RVar().Which > RVar().MaxValue ) {
					RVar().MaxValue = RVar().Which;
					RVar().maxValueDate = MDHM;
				}
				if ( RVar().Which < RVar().MinValue ) {
					RVar().MinValue = RVar().Which;
					RVar().minValueDate = MDHM;
				}
				RVar().TSValue += RVar().Which;
				RVar().EITSValue = RVar().TSValue; //CR - 8481 fix - 09/06/2011
			}

			// End of "record keeping"  Report if applicable
			if ( ! RVar().Report ) continue;
			ReportNow = true;
			if ( RVar().SchedPtr > 0 ) ReportNow = ( GetCurrentScheduleValue( RVar().SchedPtr ) != 0.0 ); // SetReportNow(RVar%SchedPtr)
			if ( ! ReportNow ) continue;
			RVar().tsStored = true;
			if ( ! RVar().thisTSStored ) {
				++RVar().thisTSCount;
				RVar().thisTSStored = true;
			}

			if ( RVar().ReportFreq == ReportEach ) {
				if ( TimePrint ) {
					if ( LHourP != HourOfDay || std::abs( LStartMin - StartMinute ) > .001 || std::abs( LEndMin - TimeValue( IndexType ).CurMinute ) > .001 ) {
						CurDayType = DayOfWeek;
						if ( HolidayIndex > 0 ) {
							CurDayType = 7 + HolidayIndex;
						}
						SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileStandard, ReportEach, TimeStepStampReportNbr, TimeStepStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, HourOfDay, TimeValue( IndexType ).CurMinute, StartMinute, DSTIndicator, DayTypes( CurDayType ) );
						LHourP = HourOfDay;
						LStartMin = StartMinute;
						LEndMin = TimeValue( IndexType ).CurMinute;
					}
					TimePrint = false;
				}

				WriteRealData( RVar().ReportID, RVar().ReportIDChr, SQLdbTimeIndex, RVar().Which );

				++StdOutputRecordCount;
			}
		}

		for ( Loop = 1; Loop <= NumOfIVariable; ++Loop ) {
			if ( IVariableTypes( Loop ).IndexType != IndexType ) continue;

			// Act on the IVariables variable using the IVar structure
			IVar >>= IVariableTypes( Loop ).VarPtr;
			IVar().Stored = true;
			//      ICurVal=IVar%Which
			if ( IVar().StoreType == AveragedVar ) {
				ICurVal = IVar().Which * rxTime;
				IVar().TSValue += ICurVal;
				IVar().EITSValue = IVar().TSValue; //CR - 8481 fix - 09/06/2011
				if ( nint( ICurVal ) > IVar().MaxValue ) {
					IVar().MaxValue = nint( ICurVal ); // Record keeping for date and time go here too
					IVar().maxValueDate = MDHM; //+ TimeValue(IndexType)%TimeStep
				}
				if ( nint( ICurVal ) < IVar().MinValue ) {
					IVar().MinValue = nint( ICurVal );
					IVar().minValueDate = MDHM; //+ TimeValue(IndexType)%TimeStep
				}
			} else {
				if ( IVar().Which > IVar().MaxValue ) {
					IVar().MaxValue = IVar().Which; // Record keeping for date and time go here too
					IVar().maxValueDate = MDHM; //+ TimeValue(IndexType)%TimeStep
				}
				if ( IVar().Which < IVar().MinValue ) {
					IVar().MinValue = IVar().Which;
					IVar().minValueDate = MDHM; //+ TimeValue(IndexType)%TimeStep
				}
				IVar().TSValue += IVar().Which;
				IVar().EITSValue = IVar().TSValue; //CR - 8481 fix - 09/06/2011
			}

			if ( ! IVar().Report ) continue;
			ReportNow = true;
			if ( IVar().SchedPtr > 0 ) ReportNow = ( GetCurrentScheduleValue( IVar().SchedPtr ) != 0.0 ); //SetReportNow(IVar%SchedPtr)
			if ( ! ReportNow ) continue;
			IVar().tsStored = true;
			if ( ! IVar().thisTSStored ) {
				++IVar().thisTSCount;
				IVar().thisTSStored = true;
			}

			if ( IVar().ReportFreq == ReportEach ) {
				if ( TimePrint ) {
					if ( LHourP != HourOfDay || std::abs( LStartMin - StartMinute ) > .001 || std::abs( LEndMin - TimeValue( IndexType ).CurMinute ) > .001 ) {
						CurDayType = DayOfWeek;
						if ( HolidayIndex > 0 ) {
							CurDayType = 7 + HolidayIndex;
						}
						SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileStandard, ReportEach, TimeStepStampReportNbr, TimeStepStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, HourOfDay, TimeValue( IndexType ).CurMinute, StartMinute, DSTIndicator, DayTypes( CurDayType ) );
						LHourP = HourOfDay;
						LStartMin = StartMinute;
						LEndMin = TimeValue( IndexType ).CurMinute;
					}
					TimePrint = false;
				}
				// only time integer vars actual report as integer only is "detailed"
				WriteIntegerData( IVar().ReportID, IVar().ReportIDChr, SQLdbTimeIndex, IVar().Which, _ );
				++StdOutputRecordCount;
			}
		}

	} else {
		ShowSevereError( "Illegal Index passed to Report Variables" );

	}}

	if ( IndexType == HVACVar ) return; // All other stuff happens at the "zone" time step call to this routine.

	//! TimeStep Block (Report on Zone TimeStep)

	if ( EndTimeStepFlag ) {

		for ( IndexType = 1; IndexType <= 2; ++IndexType ) {
			for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
				if ( RVariableTypes( Loop ).IndexType != IndexType ) continue;
				RVar >>= RVariableTypes( Loop ).VarPtr;
				// Update meters on the TimeStep  (Zone)
				if ( RVar().MeterArrayPtr != 0 ) {
					if ( VarMeterArrays( RVar().MeterArrayPtr ).NumOnCustomMeters <= 0 ) {
						UpdateMeterValues( RVar().TSValue * RVar().ZoneMult * RVar().ZoneListMult, VarMeterArrays( RVar().MeterArrayPtr ).NumOnMeters, VarMeterArrays( RVar().MeterArrayPtr ).OnMeters );
					} else {
						UpdateMeterValues( RVar().TSValue * RVar().ZoneMult * RVar().ZoneListMult, VarMeterArrays( RVar().MeterArrayPtr ).NumOnMeters, VarMeterArrays( RVar().MeterArrayPtr ).OnMeters, VarMeterArrays( RVar().MeterArrayPtr ).NumOnCustomMeters, VarMeterArrays( RVar().MeterArrayPtr ).OnCustomMeters );
					}
				}
				ReportNow = true;
				if ( RVar().SchedPtr > 0 ) ReportNow = ( GetCurrentScheduleValue( RVar().SchedPtr ) != 0.0 ); //SetReportNow(RVar%SchedPtr)
				if ( ! ReportNow || ! RVar().Report ) {
					RVar().TSValue = 0.0;
				}
				//        IF (RVar%StoreType == AveragedVar) THEN
				//          RVar%Value=RVar%Value+RVar%TSValue/NumOfTimeStepInHour
				//        ELSE
				RVar().Value += RVar().TSValue;
				//        ENDIF

				if ( ! ReportNow || ! RVar().Report ) continue;

				if ( RVar().ReportFreq == ReportTimeStep ) {
					if ( TimePrint ) {
						if ( LHourP != HourOfDay || std::abs( LStartMin - StartMinute ) > .001 || std::abs( LEndMin - TimeValue( IndexType ).CurMinute ) > .001 ) {
							CurDayType = DayOfWeek;
							if ( HolidayIndex > 0 ) {
								CurDayType = 7 + HolidayIndex;
							}
							SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileStandard, ReportEach, TimeStepStampReportNbr, TimeStepStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, HourOfDay, TimeValue( IndexType ).CurMinute, StartMinute, DSTIndicator, DayTypes( CurDayType ) );
							LHourP = HourOfDay;
							LStartMin = StartMinute;
							LEndMin = TimeValue( IndexType ).CurMinute;
						}
						TimePrint = false;
					}

					WriteRealData( RVar().ReportID, RVar().ReportIDChr, SQLdbTimeIndex, RVar().TSValue );
					++StdOutputRecordCount;
				}
				RVar().TSValue = 0.0;
				RVar().thisTSStored = false;
			} // Number of R Variables

			for ( Loop = 1; Loop <= NumOfIVariable; ++Loop ) {
				if ( IVariableTypes( Loop ).IndexType != IndexType ) continue;
				IVar >>= IVariableTypes( Loop ).VarPtr;
				ReportNow = true;
				if ( IVar().SchedPtr > 0 ) ReportNow = ( GetCurrentScheduleValue( IVar().SchedPtr ) != 0.0 ); // SetReportNow(IVar%SchedPtr)
				if ( ! ReportNow ) {
					IVar().TSValue = 0.0;
				}
				//        IF (IVar%StoreType == AveragedVar) THEN
				//          IVar%Value=IVar%Value+REAL(IVar%TSValue,r64)/REAL(NumOfTimeStepInHour,r64)
				//        ELSE
				IVar().Value += IVar().TSValue;
				//        ENDIF

				if ( ! ReportNow || ! IVar().Report ) continue;

				if ( IVar().ReportFreq == ReportTimeStep ) {
					if ( TimePrint ) {
						if ( LHourP != HourOfDay || std::abs( LStartMin - StartMinute ) > .001 || std::abs( LEndMin - TimeValue( IndexType ).CurMinute ) > .001 ) {
							CurDayType = DayOfWeek;
							if ( HolidayIndex > 0 ) {
								CurDayType = 7 + HolidayIndex;
							}
							SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileStandard, ReportEach, TimeStepStampReportNbr, TimeStepStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, HourOfDay, TimeValue( IndexType ).CurMinute, StartMinute, DSTIndicator, DayTypes( CurDayType ) );
							LHourP = HourOfDay;
							LStartMin = StartMinute;
							LEndMin = TimeValue( IndexType ).CurMinute;
						}
						TimePrint = false;
					}

					WriteIntegerData( IVar().ReportID, IVar().ReportIDChr, SQLdbTimeIndex, _, IVar().TSValue );
					++StdOutputRecordCount;
				}
				IVar().TSValue = 0.0;
				IVar().thisTSStored = false;
			} // Number of I Variables
		} // Index Type (Zone or HVAC)

		UpdateMeters( MDHM );

		ReportTSMeters( StartMinute, TimeValue( 1 ).CurMinute, TimePrint );

	} // TimeStep Block

	//!   Hour Block
	if ( EndHourFlag ) {
		if ( TrackingHourlyVariables ) {
			CurDayType = DayOfWeek;
			if ( HolidayIndex > 0 ) {
				CurDayType = 7 + HolidayIndex;
			}
			SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileStandard, ReportHourly, TimeStepStampReportNbr, TimeStepStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, HourOfDay, _, _, DSTIndicator, DayTypes( CurDayType ) );
		}

		for ( IndexType = 1; IndexType <= 2; ++IndexType ) { // Zone, HVAC
			TimeValue( IndexType ).CurMinute = 0.0;
			for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
				if ( RVariableTypes( Loop ).IndexType != IndexType ) continue;
				RVar >>= RVariableTypes( Loop ).VarPtr;
				//        ReportNow=.TRUE.
				//        IF (RVar%SchedPtr > 0) &
				//          ReportNow=(GetCurrentScheduleValue(RVar%SchedPtr) /= 0.0)  !SetReportNow(RVar%SchedPtr)

				//        IF (ReportNow) THEN
				if ( RVar().tsStored ) {
					if ( RVar().StoreType == AveragedVar ) {
						RVar().Value /= double( RVar().thisTSCount );
					}
					if ( RVar().Report && RVar().ReportFreq == ReportHourly && RVar().Stored ) {
						WriteRealData( RVar().ReportID, RVar().ReportIDChr, SQLdbTimeIndex, RVar().Value );
						++StdOutputRecordCount;
						RVar().Stored = false;
					}
					RVar().StoreValue += RVar().Value;
					++RVar().NumStored;
				}
				RVar().tsStored = false;
				RVar().thisTSStored = false;
				RVar().thisTSCount = 0;
				RVar().Value = 0.0;
			} // Number of R Variables

			for ( Loop = 1; Loop <= NumOfIVariable; ++Loop ) {
				if ( IVariableTypes( Loop ).IndexType != IndexType ) continue;
				IVar >>= IVariableTypes( Loop ).VarPtr;
				//        ReportNow=.TRUE.
				//        IF (IVar%SchedPtr > 0) &
				//          ReportNow=(GetCurrentScheduleValue(IVar%SchedPtr) /= 0.0)  !SetReportNow(IVar%SchedPtr)
				//        IF (ReportNow) THEN
				if ( IVar().tsStored ) {
					if ( IVar().StoreType == AveragedVar ) {
						IVar().Value /= double( IVar().thisTSCount );
					}
					if ( IVar().Report && IVar().ReportFreq == ReportHourly && IVar().Stored ) {
						WriteIntegerData( IVar().ReportID, IVar().ReportIDChr, SQLdbTimeIndex, _, IVar().Value );
						++StdOutputRecordCount;
						IVar().Stored = false;
					}
					IVar().StoreValue += IVar().Value;
					++IVar().NumStored;
				}
				IVar().tsStored = false;
				IVar().thisTSStored = false;
				IVar().thisTSCount = 0;
				IVar().Value = 0.0;
			} // Number of I Variables
		} // IndexType (Zone or HVAC)

		ReportHRMeters();

	} // Hour Block

	if ( ! EndHourFlag ) return;

	//!    Day Block
	if ( EndDayFlag ) {
		if ( TrackingDailyVariables ) {
			CurDayType = DayOfWeek;
			if ( HolidayIndex > 0 ) {
				CurDayType = 7 + HolidayIndex;
			}
			SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileStandard, ReportDaily, DailyStampReportNbr, DailyStampReportChr, DayOfSim, DayOfSimChr, Month, DayOfMonth, _, _, _, DSTIndicator, DayTypes( CurDayType ) );
		}
		NumHoursInMonth += 24;
		for ( IndexType = 1; IndexType <= 2; ++IndexType ) {
			for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
				if ( RVariableTypes( Loop ).IndexType == IndexType ) {
					RVar >>= RVariableTypes( Loop ).VarPtr;
					WriteRealVariableOutput( ReportDaily, SQLdbTimeIndex );
				}
			} // Number of R Variables

			for ( Loop = 1; Loop <= NumOfIVariable; ++Loop ) {
				if ( IVariableTypes( Loop ).IndexType == IndexType ) {
					IVar >>= IVariableTypes( Loop ).VarPtr;
					WriteIntegerVariableOutput( ReportDaily, SQLdbTimeIndex );
				}
			} // Number of I Variables
		} // Index type (Zone or HVAC)

		ReportDYMeters();

	} // Day Block

	// Only continue if EndDayFlag is set
	if ( ! EndDayFlag ) return;

	//!  Month Block
	if ( EndMonthFlag || EndEnvrnFlag ) {
		if ( TrackingMonthlyVariables ) {
			SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileStandard, ReportMonthly, MonthlyStampReportNbr, MonthlyStampReportChr, DayOfSim, DayOfSimChr, Month );
		}
		NumHoursInSim += NumHoursInMonth;
		EndMonthFlag = false;
		for ( IndexType = 1; IndexType <= 2; ++IndexType ) { // Zone, HVAC
			for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
				if ( RVariableTypes( Loop ).IndexType == IndexType ) {
					RVar >>= RVariableTypes( Loop ).VarPtr;
					WriteRealVariableOutput( ReportMonthly, SQLdbTimeIndex );
				}
			} // Number of R Variables

			for ( Loop = 1; Loop <= NumOfIVariable; ++Loop ) {
				if ( IVariableTypes( Loop ).IndexType == IndexType ) {
					IVar >>= IVariableTypes( Loop ).VarPtr;
					WriteIntegerVariableOutput( ReportMonthly, SQLdbTimeIndex );
				}
			} // Number of I Variables
		} // IndexType (Zone, HVAC)

		ReportMNMeters();

		NumHoursInMonth = 0;
	} // Month Block

	//!  Sim/Environment Block
	if ( EndEnvrnFlag ) {
		if ( TrackingRunPeriodVariables ) {
			SQLdbTimeIndex = WriteTimeStampFormatData( OutputFileStandard, ReportSim, RunPeriodStampReportNbr, RunPeriodStampReportChr, DayOfSim, DayOfSimChr );
		}
		for ( IndexType = 1; IndexType <= 2; ++IndexType ) { // Zone, HVAC
			for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
				if ( RVariableTypes( Loop ).IndexType == IndexType ) {
					RVar >>= RVariableTypes( Loop ).VarPtr;
					WriteRealVariableOutput( ReportSim, SQLdbTimeIndex );
				}
			} // Number of R Variables

			for ( Loop = 1; Loop <= NumOfIVariable; ++Loop ) {
				if ( IVariableTypes( Loop ).IndexType == IndexType ) {
					IVar >>= IVariableTypes( Loop ).VarPtr;
					WriteIntegerVariableOutput( ReportSim, SQLdbTimeIndex );
				}
			} // Number of I Variables
		} // Index Type (Zone, HVAC)

		ReportSMMeters();

		NumHoursInSim = 0;
	}

}

void
AssignReportNumber( int & ReportNumber )
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda K. Lawrie
	//       DATE WRITTEN   December 1997
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine returns the next report number available.  The report number
	// is used in output reports as a key.

	// METHODOLOGY EMPLOYED:
	// Use internal ReportNumberCounter to maintain current report numbers.

	// REFERENCES:
	// na

	// USE STATEMENTS:
	// na

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// FUNCTION PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// FUNCTION LOCAL VARIABLE DECLARATIONS:
	static int ReportNumberCounter( 0 );

	++ReportNumberCounter;
	ReportNumber = ReportNumberCounter;

}

void
GenOutputVariablesAuditReport()
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda Lawrie
	//       DATE WRITTEN   February 2000
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine reports (to the .err file) any report variables
	// which were requested but not "setup" during the run.  These will
	// either be items that were not used in the IDF file or misspellings
	// of report variable names.

	// METHODOLOGY EMPLOYED:
	// Use flagged data structure in OutputProcessor.

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;
	using DataGlobals::DisplayAdvancedReportVariables;
	using InputProcessor::MakeUPPERCase;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:
	// na

	// SUBROUTINE PARAMETER DEFINITIONS:
	static FArray1D_Fstring const ReportFrequency( {-1,4}, sFstring( 8 ), { "Detailed", "Timestep", "Hourly  ", "Daily   ", "Monthly ", "Annual  " } );

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	static bool Rept( false );
	int Loop;
	static bool OpaqSurfWarned( false );

	for ( Loop = 1; Loop <= NumOfReqVariables; ++Loop ) {
		if ( ReqRepVars( Loop ).Used ) continue;
		if ( ReqRepVars( Loop ).Key == "    " ) ReqRepVars( Loop ).Key = "*";
		if ( index( ReqRepVars( Loop ).VarName, "OPAQUE SURFACE INSIDE FACE CONDUCTION" ) > 0 && ! DisplayAdvancedReportVariables && ! OpaqSurfWarned ) {
			ShowWarningError( "Variables containing \"Opaque Surface Inside Face Conduction\" are now \"advanced\" variables." );
			ShowContinueError( "You must enter the \"Output:Diagnostics,DisplayAdvancedReportVariables;\" statement to view." );
			ShowContinueError( "First, though, read cautionary statements in the \"InputOutputReference\" document." );
			OpaqSurfWarned = true;
		}
		if ( ! Rept ) {
			ShowWarningError( "The following Report Variables were requested but not generated" );
			ShowContinueError( "because IDF did not contain these elements or misspelled variable name -- check .rdd file" );
			Rept = true;
		}
		ShowMessage( "Key=" + trim( ReqRepVars( Loop ).Key ) + ", VarName=" + trim( ReqRepVars( Loop ).VarName ) + ", Frequency=" + trim( ReportFrequency( ReqRepVars( Loop ).ReportFreq ) ) );
	}

}

void
UpdateMeterReporting()
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda Lawrie
	//       DATE WRITTEN   January 2001
	//       MODIFIED       February 2007 -- add cumulative meter reporting
	//                      January 2012 -- add predefined tabular meter reporting
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine is called at the end of the first HVAC iteration and
	// sets up the reporting for the Energy Meters.  It also may show a fatal error
	// if errors occurred during initial SetupOutputVariable processing.  It "gets"
	// the Report Meter input:
	// Report Meter,
	//        \memo Meters requested here show up on eplusout.eso and eplusout.mtr
	//   A1 , \field Meter_Name
	//        \required-field
	//        \note Form is EnergyUseType:..., e.g. Electricity:* for all Electricity meters
	//        \note or EndUse:..., e.g. InteriorLights:* for all interior lights
	//        \note Report MeterFileOnly puts results on the eplusout.mtr file only
	//   A2 ; \field Reporting_Frequency
	//        \type choice
	//        \key timestep
	//        \note timestep refers to the zone timestep/timestep in hour value
	//        \note runperiod, environment, and annual are the same
	//        \key hourly
	//        \key daily
	//        \key monthly
	//        \key runperiod
	//        \key environment
	//        \key annual
	//        \note runperiod, environment, and annual are synonymous
	// Report MeterFileOnly,
	//        \memo same reporting as Report Meter -- goes to eplusout.mtr only
	//   A1 , \field Meter_Name
	//        \required-field
	//        \note Form is EnergyUseType:..., e.g. Electricity:* for all Electricity meters
	//        \note or EndUse:..., e.g. InteriorLights:* for all interior lights
	//        \note Report MeterFileOnly puts results on the eplusout.mtr file only
	//   A2 ; \field Reporting_Frequency
	//        \type choice
	//        \key timestep
	//        \note timestep refers to the zone timestep/timestep in hour value
	//        \note runperiod, environment, and annual are the same
	//        \key hourly
	//        \key daily
	//        \key monthly
	//        \key runperiod
	//        \key environment
	//        \key annual
	//        \note runperiod, environment, and annual are synonymous

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataIPShortCuts;
	using namespace DataPrecisionGlobals;
	using namespace InputProcessor;
	using namespace OutputProcessor;
	using General::TrimSigDigits;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:
	// na

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	int Loop;
	FArray1D_Fstring Alphas( 2, sFstring( MaxNameLength ) );
	FArray1D< Real64 > Numbers( 1 );
	int NumAlpha;
	int NumNumbers;
	int IOStat;
	int WildCard;
	int TestLen;
	int varnameLen;
	int NumReqMeters;
	int NumReqMeterFOs;
	int Meter;
	int ReportFreq;
	bool NeverFound;

	static bool ErrorsFound( false ); // If errors detected in input

	GetCustomMeterInput( ErrorsFound );
	if ( ErrorsFound ) {
		ErrorsLogged = true;
	}

	cCurrentModuleObject = "Output:Meter";
	NumReqMeters = GetNumObjectsFound( cCurrentModuleObject );

	for ( Loop = 1; Loop <= NumReqMeters; ++Loop ) {

		GetObjectItem( cCurrentModuleObject, Loop, Alphas, NumAlpha, Numbers, NumNumbers, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );

		varnameLen = index( Alphas( 1 ), "[" );
		if ( varnameLen != 0 ) Alphas( 1 ) = Alphas( 1 )( 1, varnameLen - 1 );

		WildCard = index( Alphas( 1 ), "*" );
		if ( WildCard != 0 ) {
			TestLen = WildCard - 1;
		}

		DetermineFrequency( Alphas( 2 ), ReportFreq );

		if ( WildCard == 0 ) {
			Meter = FindItem( Alphas( 1 ), EnergyMeters.Name(), NumEnergyMeters );
			if ( Meter == 0 ) {
				ShowWarningError( trim( cCurrentModuleObject ) + ": invalid " + trim( cAlphaFieldNames( 1 ) ) + "=\"" + trim( Alphas( 1 ) ) + "\" - not found." );
				continue;
			}

			SetInitialMeterReportingAndOutputNames( Meter, false, ReportFreq, false );

		} else { // Wildcard input
			NeverFound = true;
			for ( Meter = 1; Meter <= NumEnergyMeters; ++Meter ) {
				if ( ! SameString( EnergyMeters( Meter ).Name( {1,TestLen} ), Alphas( 1 )( 1, TestLen ) ) ) continue;
				NeverFound = false;

				SetInitialMeterReportingAndOutputNames( Meter, false, ReportFreq, false );

			}
			if ( NeverFound ) {
				ShowWarningError( trim( cCurrentModuleObject ) + ": invalid " + trim( cAlphaFieldNames( 1 ) ) + "=\"" + trim( Alphas( 1 ) ) + "\" - not found." );
			}
		}

	}

	cCurrentModuleObject = "Output:Meter:MeterFileOnly";
	NumReqMeterFOs = GetNumObjectsFound( cCurrentModuleObject );
	for ( Loop = 1; Loop <= NumReqMeterFOs; ++Loop ) {

		GetObjectItem( cCurrentModuleObject, Loop, Alphas, NumAlpha, Numbers, NumNumbers, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );

		varnameLen = index( Alphas( 1 ), "[" );
		if ( varnameLen != 0 ) Alphas( 1 ) = Alphas( 1 )( 1, varnameLen - 1 );

		WildCard = index( Alphas( 1 ), "*" );
		if ( WildCard != 0 ) {
			TestLen = WildCard - 1;
		}

		DetermineFrequency( Alphas( 2 ), ReportFreq );

		if ( WildCard == 0 ) {
			Meter = FindItem( Alphas( 1 ), EnergyMeters.Name(), NumEnergyMeters );
			if ( Meter == 0 ) {
				ShowWarningError( trim( cCurrentModuleObject ) + ": invalid " + trim( cAlphaFieldNames( 1 ) ) + "=\"" + trim( Alphas( 1 ) ) + "\" - not found." );
				continue;
			}

			SetInitialMeterReportingAndOutputNames( Meter, true, ReportFreq, false );

		} else { // Wildcard input
			NeverFound = true;
			for ( Meter = 1; Meter <= NumEnergyMeters; ++Meter ) {
				if ( ! SameString( EnergyMeters( Meter ).Name( {1,TestLen} ), Alphas( 1 )( 1, TestLen ) ) ) continue;
				NeverFound = false;

				SetInitialMeterReportingAndOutputNames( Meter, true, ReportFreq, false );

			}
			if ( NeverFound ) {
				ShowWarningError( trim( cCurrentModuleObject ) + ": invalid " + trim( cAlphaFieldNames( 1 ) ) + "=\"" + trim( Alphas( 1 ) ) + "\" - not found." );
			}
		}

	}

	cCurrentModuleObject = "Output:Meter:Cumulative";
	NumReqMeters = GetNumObjectsFound( cCurrentModuleObject );

	for ( Loop = 1; Loop <= NumReqMeters; ++Loop ) {

		GetObjectItem( cCurrentModuleObject, Loop, Alphas, NumAlpha, Numbers, NumNumbers, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );

		varnameLen = index( Alphas( 1 ), "[" );
		if ( varnameLen != 0 ) Alphas( 1 ) = Alphas( 1 )( 1, varnameLen - 1 );

		WildCard = index( Alphas( 1 ), "*" );
		if ( WildCard != 0 ) {
			TestLen = WildCard - 1;
		}

		DetermineFrequency( Alphas( 2 ), ReportFreq );

		if ( WildCard == 0 ) {
			Meter = FindItem( Alphas( 1 ), EnergyMeters.Name(), NumEnergyMeters );
			if ( Meter == 0 ) {
				ShowWarningError( trim( cCurrentModuleObject ) + ": invalid " + trim( cAlphaFieldNames( 1 ) ) + "=\"" + trim( Alphas( 1 ) ) + "\" - not found." );
				continue;
			}

			SetInitialMeterReportingAndOutputNames( Meter, false, ReportFreq, true );

		} else { // Wildcard input
			NeverFound = true;
			for ( Meter = 1; Meter <= NumEnergyMeters; ++Meter ) {
				if ( ! SameString( EnergyMeters( Meter ).Name( {1,TestLen} ), Alphas( 1 )( 1, TestLen ) ) ) continue;
				NeverFound = false;

				SetInitialMeterReportingAndOutputNames( Meter, false, ReportFreq, true );

			}
			if ( NeverFound ) {
				ShowWarningError( trim( cCurrentModuleObject ) + ": invalid " + trim( cAlphaFieldNames( 1 ) ) + "=\"" + trim( Alphas( 1 ) ) + "\" - not found." );
			}
		}

	}

	cCurrentModuleObject = "Output:Meter:Cumulative:MeterFileOnly";
	NumReqMeterFOs = GetNumObjectsFound( cCurrentModuleObject );
	for ( Loop = 1; Loop <= NumReqMeterFOs; ++Loop ) {

		GetObjectItem( cCurrentModuleObject, Loop, Alphas, NumAlpha, Numbers, NumNumbers, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );

		varnameLen = index( Alphas( 1 ), "[" );
		if ( varnameLen != 0 ) Alphas( 1 ) = Alphas( 1 )( 1, varnameLen - 1 );

		WildCard = index( Alphas( 1 ), "*" );
		if ( WildCard != 0 ) {
			TestLen = WildCard - 1;
		}

		DetermineFrequency( Alphas( 2 ), ReportFreq );

		if ( WildCard == 0 ) {
			Meter = FindItem( Alphas( 1 ), EnergyMeters.Name(), NumEnergyMeters );
			if ( Meter == 0 ) {
				ShowWarningError( trim( cCurrentModuleObject ) + ": invalid " + trim( cAlphaFieldNames( 1 ) ) + "=\"" + trim( Alphas( 1 ) ) + "\" - not found." );
				continue;
			}

			SetInitialMeterReportingAndOutputNames( Meter, true, ReportFreq, true );

		} else { // Wildcard input
			NeverFound = true;
			for ( Meter = 1; Meter <= NumEnergyMeters; ++Meter ) {
				if ( ! SameString( EnergyMeters( Meter ).Name( {1,TestLen} ), Alphas( 1 )( 1, TestLen ) ) ) continue;
				NeverFound = false;

				SetInitialMeterReportingAndOutputNames( Meter, true, ReportFreq, true );

			}
			if ( NeverFound ) {
				ShowWarningError( trim( cCurrentModuleObject ) + ": invalid " + trim( cAlphaFieldNames( 1 ) ) + "=\"" + trim( Alphas( 1 ) ) + "\" - not found." );
			}
		}

	}

	ReportMeterDetails();

	if ( ErrorsLogged ) {
		ShowFatalError( "UpdateMeterReporting: Previous Meter Specification errors cause program termination." );
	}

	MeterValue.allocate( NumEnergyMeters );
	MeterValue = 0.0;

}

void
SetInitialMeterReportingAndOutputNames(
	int const WhichMeter, // Which meter number
	bool const MeterFileOnlyIndicator, // true if this is a meter file only reporting
	int const FrequencyIndicator, // at what frequency is the meter reported
	bool const CumulativeIndicator // true if this is a Cumulative meter reporting
)
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda Lawrie
	//       DATE WRITTEN   February 2007
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// Set values and output initial names to output files.

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;
	using General::TrimSigDigits;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	int indexGroupKey;
	Fstring indexGroup( MaxNameLength );

	{ auto const SELECT_CASE_var( FrequencyIndicator );
	if ( ( SELECT_CASE_var >= -1 ) && ( SELECT_CASE_var <= 0 ) ) { // roll "detailed" into TimeStep
		if ( ! CumulativeIndicator ) {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptTS ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"" + trim( EnergyMeters( WhichMeter ).Name ) + "\" (TimeStep), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptTS ) {
				EnergyMeters( WhichMeter ).RptTS = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptTSFO = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).TSRptNum, indexGroupKey, indexGroup, EnergyMeters( WhichMeter ).TSRptNumChr, EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, false, MeterFileOnlyIndicator );
			}
		} else {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptAccTS ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"Cumulative " + trim( EnergyMeters( WhichMeter ).Name ) + "\" (TimeStep), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptAccTS ) {
				EnergyMeters( WhichMeter ).RptAccTS = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptAccTSFO = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).TSAccRptNum, indexGroupKey, indexGroup, TrimSigDigits( EnergyMeters( WhichMeter ).TSAccRptNum ), EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, true, MeterFileOnlyIndicator );
			}
		}
	} else if ( SELECT_CASE_var == 1 ) {
		if ( ! CumulativeIndicator ) {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptHR ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"" + trim( EnergyMeters( WhichMeter ).Name ) + "\" (Hourly), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptHR ) {
				EnergyMeters( WhichMeter ).RptHR = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptHRFO = true;
				if ( ! MeterFileOnlyIndicator ) TrackingHourlyVariables = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).HRRptNum, indexGroupKey, indexGroup, EnergyMeters( WhichMeter ).HRRptNumChr, EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, false, MeterFileOnlyIndicator );
			}
		} else {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptAccHR ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"Cumulative " + trim( EnergyMeters( WhichMeter ).Name ) + "\" (Hourly), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptAccHR ) {
				EnergyMeters( WhichMeter ).RptAccHR = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptAccHRFO = true;
				if ( ! MeterFileOnlyIndicator ) TrackingHourlyVariables = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).HRAccRptNum, indexGroupKey, indexGroup, TrimSigDigits( EnergyMeters( WhichMeter ).HRAccRptNum ), EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, true, MeterFileOnlyIndicator );
			}
		}
	} else if ( SELECT_CASE_var == 2 ) {
		if ( ! CumulativeIndicator ) {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptDY ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"" + trim( EnergyMeters( WhichMeter ).Name ) + "\" (Daily), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptDY ) {
				EnergyMeters( WhichMeter ).RptDY = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptDYFO = true;
				if ( ! MeterFileOnlyIndicator ) TrackingDailyVariables = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).DYRptNum, indexGroupKey, indexGroup, EnergyMeters( WhichMeter ).DYRptNumChr, EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, false, MeterFileOnlyIndicator );
			}
		} else {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptAccDY ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"Cumulative " + trim( EnergyMeters( WhichMeter ).Name ) + "\" (Hourly), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptAccDY ) {
				EnergyMeters( WhichMeter ).RptAccDY = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptAccDYFO = true;
				if ( ! MeterFileOnlyIndicator ) TrackingDailyVariables = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).DYAccRptNum, indexGroupKey, indexGroup, TrimSigDigits( EnergyMeters( WhichMeter ).DYAccRptNum ), EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, true, MeterFileOnlyIndicator );
			}
		}
	} else if ( SELECT_CASE_var == 3 ) {
		if ( ! CumulativeIndicator ) {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptMN ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"" + trim( EnergyMeters( WhichMeter ).Name ) + "\" (Monthly), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptMN ) {
				EnergyMeters( WhichMeter ).RptMN = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptMNFO = true;
				if ( ! MeterFileOnlyIndicator ) TrackingMonthlyVariables = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).MNRptNum, indexGroupKey, indexGroup, EnergyMeters( WhichMeter ).MNRptNumChr, EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, false, MeterFileOnlyIndicator );
			}
		} else {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptAccMN ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"Cumulative " + trim( EnergyMeters( WhichMeter ).Name ) + "\" (Monthly), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptAccMN ) {
				EnergyMeters( WhichMeter ).RptAccMN = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptAccMNFO = true;
				if ( ! MeterFileOnlyIndicator ) TrackingMonthlyVariables = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).MNAccRptNum, indexGroupKey, indexGroup, TrimSigDigits( EnergyMeters( WhichMeter ).MNAccRptNum ), EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, true, MeterFileOnlyIndicator );
			}
		}
	} else if ( SELECT_CASE_var == 4 ) {
		if ( ! CumulativeIndicator ) {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptSM ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"" + trim( EnergyMeters( WhichMeter ).Name ) + "\" (RunPeriod), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptSM ) {
				EnergyMeters( WhichMeter ).RptSM = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptSMFO = true;
				if ( ! MeterFileOnlyIndicator ) TrackingRunPeriodVariables = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).SMRptNum, indexGroupKey, indexGroup, EnergyMeters( WhichMeter ).SMRptNumChr, EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, false, MeterFileOnlyIndicator );
			}
		} else {
			if ( MeterFileOnlyIndicator ) {
				if ( EnergyMeters( WhichMeter ).RptAccSM ) {
					ShowWarningError( "Output:Meter:MeterFileOnly requested for \"Cumulative " + trim( EnergyMeters( WhichMeter ).Name ) + "\" (RunPeriod), " "already on \"Output:Meter\". Will report to both eplusout.eso and eplusout.mtr." );
				}
			}
			if ( ! EnergyMeters( WhichMeter ).RptAccSM ) {
				EnergyMeters( WhichMeter ).RptAccSM = true;
				if ( MeterFileOnlyIndicator ) EnergyMeters( WhichMeter ).RptAccSMFO = true;
				if ( ! MeterFileOnlyIndicator ) TrackingRunPeriodVariables = true;
				indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( WhichMeter ).Name );
				indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( WhichMeter ) );
				WriteMeterDictionaryItem( FrequencyIndicator, SummedVar, EnergyMeters( WhichMeter ).SMAccRptNum, indexGroupKey, indexGroup, TrimSigDigits( EnergyMeters( WhichMeter ).SMAccRptNum ), EnergyMeters( WhichMeter ).Name, EnergyMeters( WhichMeter ).Units, true, MeterFileOnlyIndicator );
			}
		}
	} else {
	}}

}

int
GetMeterIndex( Fstring const & MeterName )
{

	// FUNCTION INFORMATION:
	//       AUTHOR         Linda K. Lawrie
	//       DATE WRITTEN   August 2002
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS FUNCTION:
	// This function returns a index to the meter "number" (aka assigned report number)
	// for the meter name.  If none active for this run, a zero is returned.  This is used later to
	// obtain a meter "value".

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;
	using InputProcessor::MakeUPPERCase;
	using InputProcessor::FindItemInSortedList;
	using SortAndStringUtilities::SetupAndSort;

	// Return value
	int MeterIndex;

	// Locals
	// FUNCTION ARGUMENT DEFINITIONS:

	// FUNCTION PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// FUNCTION LOCAL VARIABLE DECLARATIONS:
	// Valid Meter names because matching case insensitive
	static FArray1D_Fstring ValidMeterNames( sFstring( MaxNameLength ) );
	static FArray1D_int iValidMeterNames;
	static int NumValidMeters( 0 );
	int Found;
	static bool FirstCall( true );

	if ( FirstCall ) {
		NumValidMeters = NumEnergyMeters;
		ValidMeterNames.allocate( NumValidMeters );
		iValidMeterNames.allocate( NumValidMeters );
		iValidMeterNames = 0;
		for ( Found = 1; Found <= NumValidMeters; ++Found ) {
			ValidMeterNames( Found ) = MakeUPPERCase( EnergyMeters( Found ).Name );
		}
		FirstCall = false;
		SetupAndSort( ValidMeterNames, iValidMeterNames );
	} else if ( NumValidMeters != NumEnergyMeters ) {
		ValidMeterNames.deallocate();
		iValidMeterNames.deallocate();
		NumValidMeters = NumEnergyMeters;
		ValidMeterNames.allocate( NumValidMeters );
		iValidMeterNames.allocate( NumValidMeters );
		iValidMeterNames = 0;
		for ( Found = 1; Found <= NumValidMeters; ++Found ) {
			ValidMeterNames( Found ) = MakeUPPERCase( EnergyMeters( Found ).Name );
		}
		SetupAndSort( ValidMeterNames, iValidMeterNames );
	}

	MeterIndex = FindItemInSortedList( MakeUPPERCase( MeterName ), ValidMeterNames, NumValidMeters );
	if ( MeterIndex != 0 ) MeterIndex = iValidMeterNames( MeterIndex );

	return MeterIndex;

}

Fstring
GetMeterResourceType( int const MeterNumber ) // Which Meter Number (from GetMeterIndex)
{

	// FUNCTION INFORMATION:
	//       AUTHOR         Linda K. Lawrie
	//       DATE WRITTEN   August 2002
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS FUNCTION:
	// This function returns the character string of Resource Type for the
	// given meter number/index. If MeterNumber is 0, ResourceType=Invalid/Unknown.

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;

	// Return value
	Fstring ResourceType( MaxNameLength );

	// Locals
	// FUNCTION ARGUMENT DEFINITIONS:

	// FUNCTION PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// FUNCTION LOCAL VARIABLE DECLARATIONS:
	if ( MeterNumber > 0 ) {
		ResourceType = EnergyMeters( MeterNumber ).ResourceType;
	} else {
		ResourceType = "Invalid/Unknown";
	}

	return ResourceType;

}

Real64
GetCurrentMeterValue( int const MeterNumber ) // Which Meter Number (from GetMeterIndex)
{

	// FUNCTION INFORMATION:
	//       AUTHOR         Linda K. Lawrie
	//       DATE WRITTEN   August 2002
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS FUNCTION:
	// This function returns the current meter value (timestep) for the meter number indicated.

	// METHODOLOGY EMPLOYED:
	// Uses internal EnergyMeters structure to get value.

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;

	// Return value
	Real64 CurrentMeterValue;

	// Locals
	// FUNCTION ARGUMENT DEFINITIONS:

	// FUNCTION PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// FUNCTION LOCAL VARIABLE DECLARATIONS:
	// na

	if ( MeterNumber > 0 ) {
		CurrentMeterValue = EnergyMeters( MeterNumber ).CurTSValue;
	} else {
		CurrentMeterValue = 0.0;
	}

	return CurrentMeterValue;

}

Real64
GetInstantMeterValue(
	int const MeterNumber, // Which Meter Number (from GetMeterIndex)
	int const IndexType // Whether this is zone of HVAC
)
{

	// FUNCTION INFORMATION:
	//       AUTHOR         Richard Liesen
	//       DATE WRITTEN   February 2003
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS FUNCTION:
	// This function returns the Instantaneous meter value (timestep) for the meter number indicated
	//  using IndexType to differentiate between Zone and HVAC values.

	// METHODOLOGY EMPLOYED:
	// Uses internal EnergyMeters structure to get value.

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;

	// Return value
	Real64 InstantMeterValue;

	// Locals
	// FUNCTION ARGUMENT DEFINITIONS:

	// FUNCTION PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// FUNCTION LOCAL VARIABLE DECLARATIONS:
	int Loop;
	int Meter;

	InstantMeterValue = 0.0;
	//      EnergyMeters(Meter)%TSValue=EnergyMeters(EnergyMeters(Meter)%SourceMeter)%TSValue-MeterValue(Meter)

	if ( MeterNumber == 0 ) {
		InstantMeterValue = 0.0;
	} else if ( EnergyMeters( MeterNumber ).TypeOfMeter != MeterType_CustomDec ) {
		// section added to speed up the execution of this routine
		// instead of looping through all the VarMeterArrays to see if a RVariableType is used for a
		// specific meter, create a list of all the indexes for RVariableType that are used for that
		// meter.
		if ( EnergyMeters( MeterNumber ).InstMeterCacheStart == 0 ) { //not yet added to the cache
			for ( Loop = 1; Loop <= NumVarMeterArrays; ++Loop ) {
				for ( Meter = 1; Meter <= VarMeterArrays( Loop ).NumOnMeters; ++Meter ) {
					if ( VarMeterArrays( Loop ).OnMeters( Meter ) == MeterNumber ) {
						IncrementInstMeterCache();
						EnergyMeters( MeterNumber ).InstMeterCacheEnd = InstMeterCacheLastUsed;
						if ( EnergyMeters( MeterNumber ).InstMeterCacheStart == 0 ) {
							EnergyMeters( MeterNumber ).InstMeterCacheStart = InstMeterCacheLastUsed;
						}
						InstMeterCache( InstMeterCacheLastUsed ) = VarMeterArrays( Loop ).RepVariable;
						break;
					}
				}
				for ( Meter = 1; Meter <= VarMeterArrays( Loop ).NumOnCustomMeters; ++Meter ) {
					if ( VarMeterArrays( Loop ).OnCustomMeters( Meter ) == MeterNumber ) {
						IncrementInstMeterCache();
						EnergyMeters( MeterNumber ).InstMeterCacheEnd = InstMeterCacheLastUsed;
						if ( EnergyMeters( MeterNumber ).InstMeterCacheStart == 0 ) {
							EnergyMeters( MeterNumber ).InstMeterCacheStart = InstMeterCacheLastUsed;
						}
						InstMeterCache( InstMeterCacheLastUsed ) = VarMeterArrays( Loop ).RepVariable;
						break;
					}
				} // End Number of Meters Loop
			}
		}
		for ( Loop = EnergyMeters( MeterNumber ).InstMeterCacheStart; Loop <= EnergyMeters( MeterNumber ).InstMeterCacheEnd; ++Loop ) {
			RVar >>= RVariableTypes( InstMeterCache( Loop ) ).VarPtr;
			//Separate the Zone variables from the HVAC variables using IndexType
			if ( RVariableTypes( InstMeterCache( Loop ) ).IndexType == IndexType ) {
				//Add to the total all of the appropriate variables
				InstantMeterValue += RVar().Which * RVar().ZoneMult * RVar().ZoneListMult;
			}
		}
	} else { // MeterType_CustomDec
		// Get Source Meter value
		//Loop through all report meters to find correct report variables to add to instant meter total
		for ( Loop = 1; Loop <= NumVarMeterArrays; ++Loop ) {

			for ( Meter = 1; Meter <= VarMeterArrays( Loop ).NumOnMeters; ++Meter ) {
				if ( VarMeterArrays( Loop ).OnMeters( Meter ) == EnergyMeters( MeterNumber ).SourceMeter ) {
					RVar >>= RVariableTypes( VarMeterArrays( Loop ).RepVariable ).VarPtr;
					//Separate the Zone variables from the HVAC variables using IndexType
					if ( RVariableTypes( VarMeterArrays( Loop ).RepVariable ).IndexType == IndexType ) {
						//Add to the total all of the appropriate variables
						InstantMeterValue += RVar().Which * RVar().ZoneMult * RVar().ZoneListMult;
						break;
					}
				}
			}

			for ( Meter = 1; Meter <= VarMeterArrays( Loop ).NumOnCustomMeters; ++Meter ) {
				if ( VarMeterArrays( Loop ).OnCustomMeters( Meter ) == EnergyMeters( MeterNumber ).SourceMeter ) {
					RVar >>= RVariableTypes( VarMeterArrays( Loop ).RepVariable ).VarPtr;
					//Separate the Zone variables from the HVAC variables using IndexType
					if ( RVariableTypes( VarMeterArrays( Loop ).RepVariable ).IndexType == IndexType ) {
						//Add to the total all of the appropriate variables
						InstantMeterValue += RVar().Which * RVar().ZoneMult * RVar().ZoneListMult;
						break;
					}
				}
			}

		} // End Number of Meters Loop
		for ( Loop = 1; Loop <= NumVarMeterArrays; ++Loop ) {

			for ( Meter = 1; Meter <= VarMeterArrays( Loop ).NumOnMeters; ++Meter ) {
				if ( VarMeterArrays( Loop ).OnMeters( Meter ) == MeterNumber ) {
					RVar >>= RVariableTypes( VarMeterArrays( Loop ).RepVariable ).VarPtr;
					//Separate the Zone variables from the HVAC variables using IndexType
					if ( RVariableTypes( VarMeterArrays( Loop ).RepVariable ).IndexType == IndexType ) {
						//Add to the total all of the appropriate variables
						InstantMeterValue -= RVar().Which * RVar().ZoneMult * RVar().ZoneListMult;
						break;
					}
				}
			}

			for ( Meter = 1; Meter <= VarMeterArrays( Loop ).NumOnCustomMeters; ++Meter ) {
				if ( VarMeterArrays( Loop ).OnCustomMeters( Meter ) == MeterNumber ) {
					RVar >>= RVariableTypes( VarMeterArrays( Loop ).RepVariable ).VarPtr;
					//Separate the Zone variables from the HVAC variables using IndexType
					if ( RVariableTypes( VarMeterArrays( Loop ).RepVariable ).IndexType == IndexType ) {
						//Add to the total all of the appropriate variables
						InstantMeterValue -= RVar().Which * RVar().ZoneMult * RVar().ZoneListMult;
						break;
					}
				}
			}

		} // End Number of Meters Loop
	}

	return InstantMeterValue;

}

void
IncrementInstMeterCache()
{
	// SUBROUTINE INFORMATION:
	//       AUTHOR         Jason Glazer
	//       DATE WRITTEN   January 2013
	//       MODIFIED
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS FUNCTION:
	// Manage the InstMeterCache array

	// METHODOLOGY EMPLOYED:
	// When the array grows to large, double it.

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace OutputProcessor;

	if ( ! allocated( InstMeterCache ) ) {
		InstMeterCache.allocate( InstMeterCacheSizeInc );
		InstMeterCache = 0; //zero the entire array
		InstMeterCacheLastUsed = 1;
	} else {
		++InstMeterCacheLastUsed;
		// if larger then current size then make a temporary array of the same
		// type and put stuff into it while reallocating the main array
		if ( InstMeterCacheLastUsed > InstMeterCacheSize ) {
			InstMeterCacheCopy.allocate( InstMeterCacheSize );
			InstMeterCacheCopy = InstMeterCache;
			InstMeterCache.deallocate();
			// increment by cachesize
			InstMeterCache.allocate( InstMeterCacheSize + InstMeterCacheSizeInc );
			InstMeterCache = 0;
			InstMeterCache( {1,InstMeterCacheSize} ) = InstMeterCacheCopy;
			InstMeterCacheCopy.deallocate();
			InstMeterCacheSize += InstMeterCacheSizeInc;
		}
	}
}

Real64
GetInternalVariableValue(
	int const varType, // 1=integer, 2=real, 3=meter
	int const keyVarIndex // Array index
)
{
	// FUNCTION INFORMATION:
	//       AUTHOR         Linda K. Lawrie
	//       DATE WRITTEN   December 2000
	//       MODIFIED       August 2003, M. J. Witte
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS FUNCTION:
	// This function returns the current value of the Internal Variable assigned to
	// the varType and keyVarIndex.  Values may be accessed for REAL(r64) and integer
	// report variables and meter variables.  The variable type (varType) may be
	// determined by calling subroutine and GetVariableKeyCountandType.  The
	// index (keyVarIndex) may be determined by calling subroutine GetVariableKeys.

	// METHODOLOGY EMPLOYED:
	// Uses Internal OutputProcessor data structure to return value.

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;
	using ScheduleManager::GetCurrentScheduleValue;

	// Return value
	Real64 resultVal; // value returned

	// Locals
	// FUNCTION ARGUMENT DEFINITIONS:

	// FUNCTION PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// FUNCTION LOCAL VARIABLE DECLARATIONS:
	// na

	// Select based on variable type:  integer, real, or meter
	{ auto const SELECT_CASE_var( varType );

	if ( SELECT_CASE_var == 0 ) { // Variable not a found variable
		resultVal = 0.0;

	} else if ( SELECT_CASE_var == 1 ) { // Integer
		if ( keyVarIndex > NumOfIVariable ) {
			ShowFatalError( "GetInternalVariableValue: passed index beyond range of array." );
		}
		if ( keyVarIndex < 1 ) {
			ShowFatalError( "GetInternalVariableValue: passed index beyond range of array." );
		}

		IVar >>= IVariableTypes( keyVarIndex ).VarPtr;
		// must use %Which, %Value is always zero if variable is not a requested report variable
		resultVal = double( IVar().Which );

	} else if ( SELECT_CASE_var == 2 ) { // real
		if ( keyVarIndex > NumOfRVariable ) {
			ShowFatalError( "GetInternalVariableValue: passed index beyond range of array." );
		}
		if ( keyVarIndex < 1 ) {
			ShowFatalError( "GetInternalVariableValue: passed index beyond range of array." );
		}

		RVar >>= RVariableTypes( keyVarIndex ).VarPtr;
		// must use %Which, %Value is always zero if variable is not a requested report variable
		resultVal = RVar().Which;

	} else if ( SELECT_CASE_var == 3 ) { // Meter
		resultVal = GetCurrentMeterValue( keyVarIndex );

	} else if ( SELECT_CASE_var == 4 ) { // Schedule
		resultVal = GetCurrentScheduleValue( keyVarIndex );

	} else {
		resultVal = 0.0;

	}}

	return resultVal;
}

Real64
GetInternalVariableValueExternalInterface(
	int const varType, // 1=integer, 2=REAL(r64), 3=meter
	int const keyVarIndex // Array index
)
{
	// FUNCTION INFORMATION:
	//       AUTHOR         Thierry S. Nouidui
	//       DATE WRITTEN   August 2011
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS FUNCTION:
	// This function returns the last zone-timestep value of the Internal Variable assigned to
	// the varType and keyVarIndex.  Values may be accessed for REAL(r64) and integer
	// report variables and meter variables.  The variable type (varType) may be
	// determined by calling subroutine and GetVariableKeyCountandType.  The
	// index (keyVarIndex) may be determined by calling subroutine GetVariableKeys.

	// METHODOLOGY EMPLOYED:
	// Uses Internal OutputProcessor data structure to return value.

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;
	using ScheduleManager::GetCurrentScheduleValue;

	// Return value
	Real64 resultVal; // value returned

	// Locals
	// FUNCTION ARGUMENT DEFINITIONS:

	// FUNCTION PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// FUNCTION LOCAL VARIABLE DECLARATIONS:
	// na

	// Select based on variable type:  integer, REAL(r64), or meter
	{ auto const SELECT_CASE_var( varType );

	if ( SELECT_CASE_var == 0 ) { // Variable not a found variable
		resultVal = 0.0;

	} else if ( SELECT_CASE_var == 1 ) { // Integer
		if ( keyVarIndex > NumOfIVariable ) {
			ShowFatalError( "GetInternalVariableValueExternalInterface: passed index beyond range of array." );
		}
		if ( keyVarIndex < 1 ) {
			ShowFatalError( "GetInternalVariableValueExternalInterface: passed index beyond range of array." );
		}

		IVar >>= IVariableTypes( keyVarIndex ).VarPtr;
		// must use %EITSValue, %This is the last-zonetimestep value
		resultVal = double( IVar().EITSValue );

	} else if ( SELECT_CASE_var == 2 ) { // REAL(r64)
		if ( keyVarIndex > NumOfRVariable ) {
			ShowFatalError( "GetInternalVariableValueExternalInterface: passed index beyond range of array." );
		}
		if ( keyVarIndex < 1 ) {
			ShowFatalError( "GetInternalVariableValueExternalInterface: passed index beyond range of array." );
		}

		RVar >>= RVariableTypes( keyVarIndex ).VarPtr;
		// must use %EITSValue, %This is the last-zonetimestep value
		resultVal = RVar().EITSValue;

	} else if ( SELECT_CASE_var == 3 ) { // Meter
		resultVal = GetCurrentMeterValue( keyVarIndex );

	} else if ( SELECT_CASE_var == 4 ) { // Schedule
		resultVal = GetCurrentScheduleValue( keyVarIndex );

	} else {
		resultVal = 0.0;

	}}

	return resultVal;
}

int
GetNumMeteredVariables(
	Fstring const & ComponentType, // Given Component Type
	Fstring const & ComponentName // Given Component Name (user defined)
)
{

	// FUNCTION INFORMATION:
	//       AUTHOR         Linda Lawrie
	//       DATE WRITTEN   May 2005
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS FUNCTION:
	// This function counts the number of metered variables associated with the
	// given ComponentType/Name.   This resultant number would then be used to
	// allocate arrays for a call the GetMeteredVariables routine.

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace OutputProcessor;

	// Return value
	int NumVariables;

	// Locals
	// FUNCTION ARGUMENT DEFINITIONS:

	// FUNCTION PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// FUNCTION LOCAL VARIABLE DECLARATIONS:
	int Loop;
	int Pos;

	NumVariables = 0;
	for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
		//    Pos=INDEX(RVariableTypes(Loop)%VarName,':')
		//    IF (ComponentName /= RVariableTypes(Loop)%VarNameUC(1:Pos-1)) CYCLE
		if ( ComponentName != RVariableTypes( Loop ).KeyNameOnlyUC ) continue;
		RVar >>= RVariableTypes( Loop ).VarPtr;
		if ( RVar().MeterArrayPtr == 0 ) continue;
		++NumVariables;
	}

	return NumVariables;

}

void
GetMeteredVariables(
	Fstring const & ComponentType, // Given Component Type
	Fstring const & ComponentName, // Given Component Name (user defined)
	FArray1S_int VarIndexes, // Variable Numbers
	FArray1S_int VarTypes, // Variable Types (1=integer, 2=real, 3=meter)
	FArray1S_int IndexTypes, // Variable Index Types (1=Zone,2=HVAC)
	FArray1S_Fstring UnitsStrings, // UnitsStrings for each variable
	FArray1S_int ResourceTypes, // ResourceTypes for each variable
	Optional< FArray1S_Fstring > EndUses, // EndUses for each variable
	Optional< FArray1S_Fstring > Groups, // Groups for each variable
	Optional< FArray1S_Fstring > Names, // Variable Names for each variable
	Optional_int NumFound, // Number Found
	Optional< FArray1S_int > VarIDs // Variable Report Numbers
)
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda Lawrie
	//       DATE WRITTEN   May 2005
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This routine gets the variable names and other associated information
	// for metered variables associated with the given ComponentType/Name.

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using InputProcessor::MakeUPPERCase;
	using namespace DataGlobalConstants;
	using namespace OutputProcessor;

	// Argument array dimensioning

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	int Loop;
	int Pos;
	int NumVariables;
	int MeterPtr;
	int NumOnMeterPtr;
	int MeterNum;

	NumVariables = 0;

	for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
		//    Pos=INDEX(RVariableTypes(Loop)%VarName,':')
		//    IF (ComponentName /= RVariableTypes(Loop)%VarNameUC(1:Pos-1)) CYCLE
		if ( ComponentName != RVariableTypes( Loop ).KeyNameOnlyUC ) continue;
		RVar >>= RVariableTypes( Loop ).VarPtr;
		if ( RVar().MeterArrayPtr == 0 ) continue;
		NumOnMeterPtr = VarMeterArrays( RVar().MeterArrayPtr ).NumOnMeters;
		MeterPtr = VarMeterArrays( RVar().MeterArrayPtr ).OnMeters( 1 );
		++NumVariables;
		VarIndexes( NumVariables ) = Loop;
		VarTypes( NumVariables ) = 2;
		IndexTypes( NumVariables ) = RVariableTypes( Loop ).IndexType;
		UnitsStrings( NumVariables ) = RVariableTypes( Loop ).UnitsString;

		ResourceTypes( NumVariables ) = AssignResourceTypeNum( MakeUPPERCase( EnergyMeters( MeterPtr ).ResourceType ) );
		if ( present( Names ) ) {
			Names()( NumVariables ) = RVariableTypes( Loop ).VarNameUC;
		}
		if ( present( EndUses ) ) {
			for ( MeterNum = 1; MeterNum <= NumOnMeterPtr; ++MeterNum ) {
				MeterPtr = VarMeterArrays( RVar().MeterArrayPtr ).OnMeters( MeterNum );
				if ( EnergyMeters( MeterPtr ).EndUse != " " ) {
					EndUses()( NumVariables ) = MakeUPPERCase( EnergyMeters( MeterPtr ).EndUse );
					break;
				}
			}
		}
		if ( present( Groups ) ) {
			for ( MeterNum = 1; MeterNum <= NumOnMeterPtr; ++MeterNum ) {
				MeterPtr = VarMeterArrays( RVar().MeterArrayPtr ).OnMeters( MeterNum );
				if ( EnergyMeters( MeterPtr ).Group != " " ) {
					Groups()( NumVariables ) = MakeUPPERCase( EnergyMeters( MeterPtr ).Group );
					break;
				}
			}
		}
		if ( present( VarIDs ) ) {
			VarIDs()( NumVariables ) = RVar().ReportID;
		}
	}

	if ( present( NumFound ) ) {
		NumFound = NumVariables;
	}

}

void
GetVariableKeyCountandType(
	Fstring const & varName, // Standard variable name
	int & numKeys, // Number of keys found
	int & varType, // 0=not found, 1=integer, 2=real, 3=meter
	int & varAvgSum, // Variable  is Averaged=1 or Summed=2
	int & varStepType, // Variable time step is Zone=1 or HVAC=2
	Fstring & varUnits // Units sting, may be blank
)
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Michael J. Witte
	//       DATE WRITTEN   August 2003
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine returns the variable TYPE (Real, integer, meter, schedule, etc.)
	// (varType) whether it is an averaged or summed variable (varAvgSum),
	// whether it is a zone or HVAC time step (varStepType),
	// and the number of keynames for a given report variable or report meter name
	// (varName).  The variable type (varType) and number of keys (numKeys) are
	// used when calling subroutine GetVariableKeys to obtain a list of the
	// keynames for a particular variable and a corresponding list of indexes.

	// METHODOLOGY EMPLOYED:
	// Uses Internal OutputProcessor data structure to search for varName
	// in each of the three output data arrays:
	//       RVariableTypes - real report variables
	//       IVariableTypes - integer report variables
	//       EnergyMeters   - report meters (via GetMeterIndex function)
	//       Schedules      - specific schedule values
	// When the variable is found, the variable type (varType) is set and the
	// number of associated keys is counted.
	// varType is assigned as follows:
	//       0 = not found
	//       1 = integer
	//       2 = real
	//       3 = meter
	//       4 = schedule
	//  varAvgSum is assigned as follows:
	//       1 = averaged
	//       2 = summed
	//  varStepType is assigned as follows:
	//       1 = zone time step
	//       2 = HVAC time step

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using InputProcessor::MakeUPPERCase;
	using InputProcessor::FindItemInSortedList;
	using DataGlobals::MaxNameLength;
	using namespace OutputProcessor;
	using ScheduleManager::GetScheduleIndex;
	using ScheduleManager::GetScheduleType;
	using SortAndStringUtilities::SetupAndSort;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	static FArray1D_int keyVarIndexes; // Array index for specific key name
	static int curKeyVarIndexLimit; // current limit for keyVarIndexes
	static bool InitFlag( true ); // for initting the keyVarIndexes array
	int Loop; // Loop counters
	int Loop2;
	int Position; // Starting point of search string
	int VFound; // Found integer/real variable attributes
	bool Found; // True if varName is found
	bool Duplicate; // True if keyname is a duplicate
	Fstring VarKeyPlusName( MaxNameLength * 2 + 1 ); // Full variable name including keyname and units
	Fstring varNameUpper( MaxNameLength * 2 + 1 ); // varName pushed to all upper case
	static FArray1D_Fstring varNames( sFstring( MaxNameLength ) ); // stored variable names
	static FArray1D_int ivarNames; // pointers for sorted information
	static int numVarNames; // number of variable names

	// INITIALIZATIONS
	if ( InitFlag ) {
		curKeyVarIndexLimit = 1000;
		keyVarIndexes.allocate( curKeyVarIndexLimit );
		numVarNames = NumVariablesForOutput;
		varNames.allocate( numVarNames );
		ivarNames.allocate( numVarNames );
		ivarNames = 0;
		for ( Loop = 1; Loop <= NumVariablesForOutput; ++Loop ) {
			varNames( Loop ) = MakeUPPERCase( DDVariableTypes( Loop ).VarNameOnly );
		}
		SetupAndSort( varNames, ivarNames );
		InitFlag = false;
	}

	if ( numVarNames != NumVariablesForOutput ) {
		varNames.deallocate();
		ivarNames.deallocate();
		numVarNames = NumVariablesForOutput;
		varNames.allocate( numVarNames );
		ivarNames.allocate( numVarNames );
		ivarNames = 0;
		for ( Loop = 1; Loop <= NumVariablesForOutput; ++Loop ) {
			varNames( Loop ) = MakeUPPERCase( DDVariableTypes( Loop ).VarNameOnly );
		}
		SetupAndSort( varNames, ivarNames );
	}

	keyVarIndexes = 0;
	varType = VarType_NotFound;
	numKeys = 0;
	varAvgSum = 0;
	varStepType = 0;
	varUnits = " ";
	Found = false;
	Duplicate = false;
	varNameUpper = varName;

	// Search Variable List First
	VFound = FindItemInSortedList( varNameUpper, varNames, numVarNames );
	if ( VFound != 0 ) {
		varType = DDVariableTypes( ivarNames( VFound ) ).VariableType;
	}

	if ( varType == VarType_Integer ) {
		// Search Integer Variables
		for ( Loop = 1; Loop <= NumOfIVariable; ++Loop ) {
			VarKeyPlusName = IVariableTypes( Loop ).VarNameUC;
			Position = index( trim( VarKeyPlusName ), ":" + trim( varNameUpper ), true );
			if ( Position > 0 ) {
				if ( VarKeyPlusName( Position + 1 ) == varNameUpper ) {
					Found = true;
					varType = VarType_Integer;
					Duplicate = false;
					// Check if duplicate - duplicates happen if the same report variable/key name
					// combination is requested more than once in the idf at different reporting
					// frequencies
					for ( Loop2 = 1; Loop2 <= numKeys; ++Loop2 ) {
						if ( VarKeyPlusName == IVariableTypes( keyVarIndexes( Loop2 ) ).VarNameUC ) Duplicate = true;
					}
					if ( ! Duplicate ) {
						++numKeys;
						if ( numKeys > curKeyVarIndexLimit ) {
							ReallocateIntegerArray( keyVarIndexes, curKeyVarIndexLimit, 500 );
						}
						keyVarIndexes( numKeys ) = Loop;
						varAvgSum = DDVariableTypes( ivarNames( VFound ) ).StoreType;
						varStepType = DDVariableTypes( ivarNames( VFound ) ).IndexType;
						varUnits = DDVariableTypes( ivarNames( VFound ) ).UnitsString;
					}
				}
			}
		}
	} else if ( varType == VarType_Real ) {
		// Search real Variables Next
		for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
			if ( RVariableTypes( Loop ).VarNameOnlyUC == varNameUpper ) {
				Found = true;
				varType = VarType_Real;
				Duplicate = false;
				// Check if duplicate - duplicates happen if the same report variable/key name
				// combination is requested more than once in the idf at different reporting
				// frequencies
				VarKeyPlusName = RVariableTypes( Loop ).VarNameUC;
				for ( Loop2 = 1; Loop2 <= numKeys; ++Loop2 ) {
					if ( VarKeyPlusName == RVariableTypes( keyVarIndexes( Loop2 ) ).VarNameUC ) Duplicate = true;
				}
				if ( ! Duplicate ) {
					++numKeys;
					if ( numKeys > curKeyVarIndexLimit ) {
						ReallocateIntegerArray( keyVarIndexes, curKeyVarIndexLimit, 500 );
					}
					keyVarIndexes( numKeys ) = Loop;
					varAvgSum = DDVariableTypes( ivarNames( VFound ) ).StoreType;
					varStepType = DDVariableTypes( ivarNames( VFound ) ).IndexType;
					varUnits = DDVariableTypes( ivarNames( VFound ) ).UnitsString;
				}
			}
		}
	}

	// Search Meters if not found in integers or reals
	// Use the GetMeterIndex function
	// Meters do not have keys, so only one will be found
	if ( ! Found ) {
		keyVarIndexes( 1 ) = GetMeterIndex( varName );
		if ( keyVarIndexes( 1 ) > 0 ) {
			Found = true;
			numKeys = 1;
			varType = VarType_Meter;
			varUnits = EnergyMeters( keyVarIndexes( 1 ) ).Units;
			varAvgSum = SummedVar;
			varStepType = ZoneVar;
		}
	}

	// Search schedules if not found in integers, reals, or meters
	// Use the GetScheduleIndex function
	// Schedules do not have keys, so only one will be found
	if ( ! Found ) {
		keyVarIndexes( 1 ) = GetScheduleIndex( varName );
		if ( keyVarIndexes( 1 ) > 0 ) {
			Found = true;
			numKeys = 1;
			varType = VarType_Schedule;
			varUnits = GetScheduleType( keyVarIndexes( 1 ) );
			varAvgSum = AveragedVar;
			varStepType = ZoneVar;
		}
	}

}

void
GetVariableKeys(
	Fstring const & varName, // Standard variable name
	int const varType, // 1=integer, 2=real, 3=meter
	FArray1S_Fstring keyNames, // Specific key name
	FArray1S_int keyVarIndexes // Array index for
)
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Michael J. Witte
	//       DATE WRITTEN   August 2003
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine returns a list of keynames and indexes associated
	// with a particular report variable or report meter name (varName).
	// This routine assumes that the variable TYPE (Real, integer, meter, etc.)
	// may be determined by calling GetVariableKeyCountandType.  The variable type
	// and index can then be used with function GetInternalVariableValue to
	// to retrieve the current value of a particular variable/keyname combination.

	// METHODOLOGY EMPLOYED:
	// Uses Internal OutputProcessor data structure to search for varName
	// and build list of keynames and indexes.  The indexes are the array index
	// in the data array for the

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using InputProcessor::MakeUPPERCase;
	using namespace OutputProcessor;
	using ScheduleManager::GetScheduleIndex;

	// Argument array dimensioning

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	int Loop; // Loop counters
	int Loop2;
	int Position; // Starting point of search string
	bool Duplicate; // True if keyname is a duplicate
	int maxKeyNames; // Max allowable # of key names=size of keyNames array
	int maxkeyVarIndexes; // Max allowable # of key indexes=size of keyVarIndexes array
	int numKeys; // Number of keys found
	Fstring VarKeyPlusName( MaxNameLength * 2 + 1 ); // Full variable name including keyname and units
	Fstring varNameUpper( MaxNameLength * 2 + 1 ); // varName pushed to all upper case

	// INITIALIZATIONS
	keyNames = " ";
	keyVarIndexes = 0;
	numKeys = 0;
	Duplicate = false;
	maxKeyNames = size( keyNames );
	maxkeyVarIndexes = size( keyVarIndexes );
	varNameUpper = MakeUPPERCase( varName );

	// Select based on variable type:  integer, real, or meter
	{ auto const SELECT_CASE_var( varType );

	if ( SELECT_CASE_var == VarType_Integer ) { // Integer
		for ( Loop = 1; Loop <= NumOfIVariable; ++Loop ) {
			VarKeyPlusName = IVariableTypes( Loop ).VarNameUC;
			Position = index( trim( VarKeyPlusName ), ":" + trim( varNameUpper ), true );
			if ( Position > 0 ) {
				if ( VarKeyPlusName( Position + 1 ) == varNameUpper ) {
					Duplicate = false;
					// Check if duplicate - duplicates happen if the same report variable/key name
					// combination is requested more than once in the idf at different reporting
					// frequencies
					for ( Loop2 = 1; Loop2 <= numKeys; ++Loop2 ) {
						if ( VarKeyPlusName == IVariableTypes( keyVarIndexes( Loop2 ) ).VarNameUC ) Duplicate = true;
					}
					if ( ! Duplicate ) {
						++numKeys;
						if ( ( numKeys > maxKeyNames ) || ( numKeys > maxkeyVarIndexes ) ) {
							ShowFatalError( "Invalid array size in GetVariableKeys" );
						}
						keyNames( numKeys ) = IVariableTypes( Loop ).VarNameUC( {1,Position - 1} );
						keyVarIndexes( numKeys ) = Loop;
					}
				}
			}
		}

	} else if ( SELECT_CASE_var == VarType_Real ) { // Real
		for ( Loop = 1; Loop <= NumOfRVariable; ++Loop ) {
			if ( RVariableTypes( Loop ).VarNameOnlyUC == varNameUpper ) {
				Duplicate = false;
				// Check if duplicate - duplicates happen if the same report variable/key name
				// combination is requested more than once in the idf at different reporting
				// frequencies
				VarKeyPlusName = RVariableTypes( Loop ).VarNameUC;
				for ( Loop2 = 1; Loop2 <= numKeys; ++Loop2 ) {
					if ( VarKeyPlusName == RVariableTypes( keyVarIndexes( Loop2 ) ).VarNameUC ) Duplicate = true;
				}
				if ( ! Duplicate ) {
					++numKeys;
					if ( ( numKeys > maxKeyNames ) || ( numKeys > maxkeyVarIndexes ) ) {
						ShowFatalError( "Invalid array size in GetVariableKeys" );
					}
					keyNames( numKeys ) = RVariableTypes( Loop ).KeyNameOnlyUC;
					keyVarIndexes( numKeys ) = Loop;
				}
			}
		}

	} else if ( SELECT_CASE_var == VarType_Meter ) { // Meter
		numKeys = 1;
		if ( ( numKeys > maxKeyNames ) || ( numKeys > maxkeyVarIndexes ) ) {
			ShowFatalError( "Invalid array size in GetVariableKeys" );
		}
		keyNames( 1 ) = "Meter";
		keyVarIndexes( 1 ) = GetMeterIndex( varName );

	} else if ( SELECT_CASE_var == VarType_Schedule ) { // Schedule
		numKeys = 1;
		if ( ( numKeys > maxKeyNames ) || ( numKeys > maxkeyVarIndexes ) ) {
			ShowFatalError( "Invalid array size in GetVariableKeys" );
		}
		keyNames( 1 ) = "Environment";
		keyVarIndexes( 1 ) = GetScheduleIndex( varName );

	} else {
		// do nothing

	}}

}

bool
ReportingThisVariable( Fstring const & RepVarName )
{

	// FUNCTION INFORMATION:
	//       AUTHOR         Linda Lawrie
	//       DATE WRITTEN   October 2008
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS FUNCTION:
	// This function scans the report variables and reports back
	// if user has requested this variable be reported.

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace OutputProcessor;
	using InputProcessor::FindItem;

	// Return value
	bool BeingReported;

	// Locals
	// FUNCTION ARGUMENT DEFINITIONS:

	// FUNCTION PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// FUNCTION LOCAL VARIABLE DECLARATIONS:
	int Found;

	BeingReported = false;
	Found = FindItem( RepVarName, ReqRepVars.VarName(), NumOfReqVariables );
	if ( Found > 0 ) {
		BeingReported = true;
	}

	if ( ! BeingReported ) { // check meter names too
		Found = FindItem( RepVarName, EnergyMeters.Name(), NumEnergyMeters );
		if ( Found > 0 ) {
			if ( EnergyMeters( Found ).RptTS || EnergyMeters( Found ).RptHR || EnergyMeters( Found ).RptDY || EnergyMeters( Found ).RptMN || EnergyMeters( Found ).RptSM || EnergyMeters( Found ).RptTSFO || EnergyMeters( Found ).RptHRFO || EnergyMeters( Found ).RptDYFO || EnergyMeters( Found ).RptMNFO || EnergyMeters( Found ).RptSMFO || EnergyMeters( Found ).RptAccTS || EnergyMeters( Found ).RptAccHR || EnergyMeters( Found ).RptAccDY || EnergyMeters( Found ).RptAccMN || EnergyMeters( Found ).RptAccSM || EnergyMeters( Found ).RptAccTSFO || EnergyMeters( Found ).RptAccHRFO || EnergyMeters( Found ).RptAccDYFO || EnergyMeters( Found ).RptAccMNFO || EnergyMeters( Found ).RptAccSMFO ) {
				BeingReported = true;
			}
		}
	}

	return BeingReported;

}

void
InitPollutionMeterReporting( Fstring const & ReportFreqName )
{

	// SUBROUTINE INFORMATION:Richard Liesen
	//       DATE WRITTEN   July 2002
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This subroutine is called at the end of the first HVAC iteration and
	// sets up the reporting for the Pollution Meters.
	// ReportPollutionOutput,
	//   A1 ; \field Reporting_Frequency
	//        \type choice
	//        \key timestep
	//        \key hourly
	//        \key daily
	//        \key monthly
	//        \key runperiod
	// METHODOLOGY EMPLOYED:
	// The program tries to setup all of the following meters if the Pollution Report is initiated.
	//       Electricity:Facility [J]
	//       Diesel:Facility [J]
	//       DistrictCooling:Facility [J]
	//       DistrictHeating:Facility [J]
	//       Gas:Facility [J]
	//       GASOLINE:Facility [J]
	//       COAL:Facility [J]
	//       FuelOil#1:Facility [J]
	//       FuelOil#2:Facility [J]
	//       Propane:Facility [J]
	//       ElectricityProduced:Facility [J]
	//       Pollutant:CO2
	//       Pollutant:CO
	//       Pollutant:CH4
	//       Pollutant:NOx
	//       Pollutant:N2O
	//       Pollutant:SO2
	//       Pollutant:PM
	//       Pollutant:PM10
	//       Pollutant:PM2.5
	//       Pollutant:NH3
	//       Pollutant:NMVOC
	//       Pollutant:Hg
	//       Pollutant:Pb
	//       Pollutant:WaterEnvironmentalFactors
	//       Pollutant:Nuclear High
	//       Pollutant:Nuclear Low
	//       Pollutant:Carbon Equivalent
	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using InputProcessor::FindItem;
	using namespace OutputProcessor;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	//             Now for the Pollution Meters
	static FArray1D_Fstring const PollutionMeters( {1,29}, sFstring( 35 ), { "Electricity:Facility               ", "Diesel:Facility                    ", "DistrictCooling:Facility           ", "DistrictHeating:Facility           ", "Gas:Facility                       ", "GASOLINE:Facility                  ", "COAL:Facility                      ", "FuelOil#1:Facility                 ", "FuelOil#2:Facility                 ", "Propane:Facility                   ", "ElectricityProduced:Facility       ", "Steam:Facility                     ", "CO2:Facility                       ", "CO:Facility                        ", "CH4:Facility                       ", "NOx:Facility                       ", "N2O:Facility                       ", "SO2:Facility                       ", "PM:Facility                        ", "PM10:Facility                      ", "PM2.5:Facility                     ", "NH3:Facility                       ", "NMVOC:Facility                     ", "Hg:Facility                        ", "Pb:Facility                        ", "WaterEnvironmentalFactors:Facility ", "Nuclear High:Facility              ", "Nuclear Low:Facility               ", "Carbon Equivalent:Facility         " } );

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	int Loop;
	int NumReqMeters;
	int Meter;
	int ReportFreq;

	int indexGroupKey;
	Fstring indexGroup( MaxNameLength );

	NumReqMeters = 29;
	DetermineFrequency( ReportFreqName, ReportFreq );

	for ( Loop = 1; Loop <= NumReqMeters; ++Loop ) {

		Meter = FindItem( PollutionMeters( Loop ), EnergyMeters.Name(), NumEnergyMeters );
		if ( Meter > 0 ) { //All the active meters for this run are set, but all are still searched for.

			indexGroupKey = DetermineIndexGroupKeyFromMeterName( EnergyMeters( Meter ).Name );
			indexGroup = DetermineIndexGroupFromMeterGroup( EnergyMeters( Meter ) );
			//All of the specified meters are checked and the headers printed to the meter file if this
			//  has not been done previously
			{ auto const SELECT_CASE_var( ReportFreq );

			if ( SELECT_CASE_var == ReportTimeStep ) {
				if ( EnergyMeters( Meter ).RptTS ) {
					EnergyMeters( Meter ).RptTS = true;
				} else {
					EnergyMeters( Meter ).RptTS = true;
					WriteMeterDictionaryItem( ReportFreq, SummedVar, EnergyMeters( Meter ).TSRptNum, indexGroupKey, indexGroup, EnergyMeters( Meter ).TSRptNumChr, EnergyMeters( Meter ).Name, EnergyMeters( Meter ).Units, false, false );
				}

			} else if ( SELECT_CASE_var == ReportHourly ) {
				if ( EnergyMeters( Meter ).RptHR ) {
					EnergyMeters( Meter ).RptHR = true;
					TrackingHourlyVariables = true;
				} else {
					EnergyMeters( Meter ).RptHR = true;
					TrackingHourlyVariables = true;
					WriteMeterDictionaryItem( ReportFreq, SummedVar, EnergyMeters( Meter ).HRRptNum, indexGroupKey, indexGroup, EnergyMeters( Meter ).HRRptNumChr, EnergyMeters( Meter ).Name, EnergyMeters( Meter ).Units, false, false );
				}

			} else if ( SELECT_CASE_var == ReportDaily ) {
				if ( EnergyMeters( Meter ).RptDY ) {
					EnergyMeters( Meter ).RptDY = true;
					TrackingDailyVariables = true;
				} else {
					EnergyMeters( Meter ).RptDY = true;
					TrackingDailyVariables = true;
					WriteMeterDictionaryItem( ReportFreq, SummedVar, EnergyMeters( Meter ).DYRptNum, indexGroupKey, indexGroup, EnergyMeters( Meter ).DYRptNumChr, EnergyMeters( Meter ).Name, EnergyMeters( Meter ).Units, false, false );
				}

			} else if ( SELECT_CASE_var == ReportMonthly ) {
				if ( EnergyMeters( Meter ).RptMN ) {
					EnergyMeters( Meter ).RptMN = true;
					TrackingMonthlyVariables = true;
				} else {
					EnergyMeters( Meter ).RptMN = true;
					TrackingMonthlyVariables = true;
					WriteMeterDictionaryItem( ReportFreq, SummedVar, EnergyMeters( Meter ).MNRptNum, indexGroupKey, indexGroup, EnergyMeters( Meter ).MNRptNumChr, EnergyMeters( Meter ).Name, EnergyMeters( Meter ).Units, false, false );
				}

			} else if ( SELECT_CASE_var == ReportSim ) {
				if ( EnergyMeters( Meter ).RptSM ) {
					EnergyMeters( Meter ).RptSM = true;
					TrackingRunPeriodVariables = true;
				} else {
					EnergyMeters( Meter ).RptSM = true;
					TrackingRunPeriodVariables = true;
					WriteMeterDictionaryItem( ReportFreq, SummedVar, EnergyMeters( Meter ).SMRptNum, indexGroupKey, indexGroup, EnergyMeters( Meter ).SMRptNumChr, EnergyMeters( Meter ).Name, EnergyMeters( Meter ).Units, false, false );
				}

			} else {

			}}
		}

	}

}

void
ProduceRDDMDD()
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda Lawrie
	//       DATE WRITTEN   March 2009
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// provide a single call for writing out the Report Data Dictionary and Meter Data Dictionary.

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using DataStringGlobals::VerString;
	using DataStringGlobals::IDDVerString;
	using InputProcessor::SameString;
	using InputProcessor::FindItemInList;
	using namespace OutputProcessor;
	using SortAndStringUtilities::SetupAndSort;
	using General::ScanForReports;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:
	// na

	// SUBROUTINE PARAMETER DEFINITIONS:
	int const RealType( 1 );
	int const IntegerType( 2 );

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	Fstring VarOption1( MaxNameLength );
	Fstring VarOption2( MaxNameLength );
	bool DoReport;
	FArray1D_Fstring VariableNames( sFstring( MaxNameLength ) );
	FArray1D_int iVariableNames;
	int Item;
	bool SortByName;
	int ItemPtr;
	int write_stat;

	struct VariableTypes
	{
		// Members
		int RealIntegerType; // Real= 1, Integer=2
		int VarPtr; // pointer to real/integer VariableTypes structures
		int IndexType;
		int StoreType;
		Fstring UnitsString;

		// Default Constructor
		VariableTypes() :
			RealIntegerType( 0 ),
			VarPtr( 0 ),
			IndexType( 0 ),
			StoreType( 0 ),
			UnitsString( UnitsStringLength )
		{}

		// Member Constructor
		VariableTypes(
			int const RealIntegerType, // Real= 1, Integer=2
			int const VarPtr, // pointer to real/integer VariableTypes structures
			int const IndexType,
			int const StoreType,
			Fstring const & UnitsString
		) :
			RealIntegerType( RealIntegerType ),
			VarPtr( VarPtr ),
			IndexType( IndexType ),
			StoreType( StoreType ),
			UnitsString( UnitsStringLength, UnitsString )
		{}

	};

	//  See if Report Variables should be turned on

	SortByName = false;
	ScanForReports( "VariableDictionary", DoReport, _, VarOption1, VarOption2 );
	//  IF (.not. DoReport) RETURN

	if ( DoReport ) {
		ProduceReportVDD = ReportVDD_Yes;
		if ( VarOption1 == "IDF" ) {
			ProduceReportVDD = ReportVDD_IDF;
		}
		if ( VarOption2 != " " ) {
			if ( SameString( VarOption2, "Name" ) || SameString( VarOption2, "AscendingName" ) ) {
				SortByName = true;
			}
		}
	}

	if ( ProduceReportVDD == ReportVDD_Yes ) {
		OutputFileRVDD = GetNewUnitNumber();
		{ IOFlags flags; flags.ACTION( "write" ); gio::open( OutputFileRVDD, "eplusout.rdd", flags ); write_stat = flags.ios(); }
		if ( write_stat != 0 ) {
			ShowFatalError( "ProduceRDDMDD: Could not open file \"eplusout.rdd\" for output (write)." );
		}
		gio::write( OutputFileRVDD, fmta ) << "Program Version," + trim( VerString ) + "," + trim( IDDVerString );
		gio::write( OutputFileRVDD, fmta ) << "Var Type (reported time step),Var Report Type,Variable Name [Units]";
		OutputFileMVDD = GetNewUnitNumber();
		{ IOFlags flags; flags.ACTION( "write" ); gio::open( OutputFileMVDD, "eplusout.mdd", flags ); write_stat = flags.ios(); }
		if ( write_stat != 0 ) {
			ShowFatalError( "ProduceRDDMDD: Could not open file \"eplusout.mdd\" for output (write)." );
		}
		gio::write( OutputFileMVDD, fmta ) << "Program Version," + trim( VerString ) + "," + trim( IDDVerString );
		gio::write( OutputFileMVDD, fmta ) << "Var Type (reported time step),Var Report Type,Variable Name [Units]";
	} else if ( ProduceReportVDD == ReportVDD_IDF ) {
		OutputFileRVDD = GetNewUnitNumber();
		{ IOFlags flags; flags.ACTION( "write" ); gio::open( OutputFileRVDD, "eplusout.rdd", flags ); write_stat = flags.ios(); }
		if ( write_stat != 0 ) {
			ShowFatalError( "ProduceRDDMDD: Could not open file \"eplusout.rdd\" for output (write)." );
		}
		gio::write( OutputFileRVDD, fmta ) << "! Program Version," + trim( VerString ) + "," + trim( IDDVerString );
		gio::write( OutputFileRVDD, fmta ) << "! Output:Variable Objects (applicable to this run)";
		OutputFileMVDD = GetNewUnitNumber();
		{ IOFlags flags; flags.ACTION( "write" ); gio::open( OutputFileMVDD, "eplusout.mdd", flags ); write_stat = flags.ios(); }
		if ( write_stat != 0 ) {
			ShowFatalError( "ProduceRDDMDD: Could not open file \"eplusout.mdd\" for output (write)." );
		}
		gio::write( OutputFileMVDD, fmta ) << "! Program Version," + trim( VerString ) + "," + trim( IDDVerString );
		gio::write( OutputFileMVDD, fmta ) << "! Output:Meter Objects (applicable to this run)";
	}

	VariableNames.allocate( NumVariablesForOutput );
	VariableNames( {1,NumVariablesForOutput} ) = DDVariableTypes( {1,NumVariablesForOutput} ).VarNameOnly();
	iVariableNames.allocate( NumVariablesForOutput );
	iVariableNames = 0;

	if ( SortByName ) {
		SetupAndSort( VariableNames, iVariableNames );
	} else {
		for ( Item = 1; Item <= NumVariablesForOutput; ++Item ) {
			iVariableNames( Item ) = Item;
		}
	}

	for ( Item = 1; Item <= NumVariablesForOutput; ++Item ) {
		if ( ProduceReportVDD == ReportVDD_Yes ) {
			ItemPtr = iVariableNames( Item );
			if ( ! DDVariableTypes( ItemPtr ).ReportedOnDDFile ) {
				gio::write( OutputFileRVDD, fmta ) << trim( StandardIndexTypeKey( DDVariableTypes( ItemPtr ).IndexType ) ) + "," + trim( StandardVariableTypeKey( DDVariableTypes( ItemPtr ).StoreType ) ) + "," + trim( VariableNames( Item ) ) + " [" + trim( DDVariableTypes( ItemPtr ).UnitsString ) + "]";
				DDVariableTypes( ItemPtr ).ReportedOnDDFile = true;
				while ( DDVariableTypes( ItemPtr ).Next != 0 ) {
					if ( SortByName ) {
						++ItemPtr;
					} else {
						ItemPtr = DDVariableTypes( ItemPtr ).Next;
					}
					gio::write( OutputFileRVDD, fmta ) << trim( StandardIndexTypeKey( DDVariableTypes( ItemPtr ).IndexType ) ) + "," + trim( StandardVariableTypeKey( DDVariableTypes( ItemPtr ).StoreType ) ) + "," + trim( VariableNames( Item ) ) + " [" + trim( DDVariableTypes( ItemPtr ).UnitsString ) + "]";
					DDVariableTypes( ItemPtr ).ReportedOnDDFile = true;
				}
			}
		} else if ( ProduceReportVDD == ReportVDD_IDF ) {
			ItemPtr = iVariableNames( Item );
			if ( ! DDVariableTypes( ItemPtr ).ReportedOnDDFile ) {
				gio::write( OutputFileRVDD, fmta ) << "Output:Variable,*," + trim( VariableNames( Item ) ) + ",hourly; !- " + trim( StandardIndexTypeKey( DDVariableTypes( ItemPtr ).IndexType ) ) + " " + trim( StandardVariableTypeKey( DDVariableTypes( ItemPtr ).StoreType ) ) + " [" + trim( DDVariableTypes( ItemPtr ).UnitsString ) + "]";
				DDVariableTypes( ItemPtr ).ReportedOnDDFile = true;
				while ( DDVariableTypes( ItemPtr ).Next != 0 ) {
					if ( SortByName ) {
						++ItemPtr;
					} else {
						ItemPtr = DDVariableTypes( ItemPtr ).Next;
					}
					gio::write( OutputFileRVDD, fmta ) << "Output:Variable,*," + trim( VariableNames( Item ) ) + ",hourly; !- " + trim( StandardIndexTypeKey( DDVariableTypes( ItemPtr ).IndexType ) ) + " " + trim( StandardVariableTypeKey( DDVariableTypes( ItemPtr ).StoreType ) ) + " [" + trim( DDVariableTypes( ItemPtr ).UnitsString ) + "]";
					DDVariableTypes( ItemPtr ).ReportedOnDDFile = true;
				}
			}
		}
	}

	VariableNames.deallocate();
	iVariableNames.deallocate();

	//  Now EnergyMeter variables
	if ( SortByName ) {
		VariableNames.allocate( NumEnergyMeters );
		for ( Item = 1; Item <= NumEnergyMeters; ++Item ) {
			VariableNames( Item ) = EnergyMeters( Item ).Name;
		}
		iVariableNames.allocate( NumEnergyMeters );
		iVariableNames = 0;
		SetupAndSort( VariableNames, iVariableNames );
	} else {
		VariableNames.allocate( NumEnergyMeters );
		iVariableNames.allocate( NumEnergyMeters );
		iVariableNames = 0;

		for ( Item = 1; Item <= NumEnergyMeters; ++Item ) {
			VariableNames( Item ) = EnergyMeters( Item ).Name;
			iVariableNames( Item ) = Item;
		}
	}

	for ( Item = 1; Item <= NumEnergyMeters; ++Item ) {
		ItemPtr = iVariableNames( Item );
		if ( ProduceReportVDD == ReportVDD_Yes ) {
			gio::write( OutputFileMVDD, fmta ) << "Zone,Meter," + trim( EnergyMeters( ItemPtr ).Name ) + " [" + trim( EnergyMeters( ItemPtr ).Units ) + "]";
		} else if ( ProduceReportVDD == ReportVDD_IDF ) {
			gio::write( OutputFileMVDD, fmta ) << "Output:Meter," + trim( EnergyMeters( ItemPtr ).Name ) + ",hourly; !- [" + trim( EnergyMeters( ItemPtr ).Units ) + "]";
			gio::write( OutputFileMVDD, fmta ) << "Output:Meter:Cumulative," + trim( EnergyMeters( ItemPtr ).Name ) + ",hourly; !- [" + trim( EnergyMeters( ItemPtr ).Units ) + "]";
		}
	}

	VariableNames.deallocate();
	iVariableNames.deallocate();

	// DEALLOCATE(DDVariableTypes)

}

void
AddToOutputVariableList(
	Fstring const & VarName, // Variable Name
	int const IndexType,
	int const StateType,
	int const VariableType,
	Fstring const & UnitsString
)
{

	// SUBROUTINE INFORMATION:
	//       AUTHOR         Linda Lawrie
	//       DATE WRITTEN   August 2010
	//       MODIFIED       na
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS SUBROUTINE:
	// This routine maintains a unique list of Output Variables for the
	// Variable Dictionary output.

	// METHODOLOGY EMPLOYED:
	// na

	// REFERENCES:
	// na

	// Using/Aliasing
	using namespace OutputProcessor;
	using InputProcessor::FindItemInList;

	// Locals
	// SUBROUTINE ARGUMENT DEFINITIONS:

	// SUBROUTINE PARAMETER DEFINITIONS:
	// na

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// DERIVED TYPE DEFINITIONS:
	// na

	// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
	int dup; // for duplicate variable name
	int dup2; // for duplicate variable name

	// Object Data
	FArray1D< VariableTypeForDDOutput > tmpDDVariableTypes; // Variable Types structure (temp for reallocate)

	dup = 0;
	if ( NumVariablesForOutput > 0 ) {
		dup = FindItemInList( VarName, DDVariableTypes.VarNameOnly(), NumVariablesForOutput );
	} else {
		DDVariableTypes.allocate( LVarAllocInc );
		MaxVariablesForOutput = LVarAllocInc;
	}
	if ( dup == 0 ) {
		++NumVariablesForOutput;
		if ( NumVariablesForOutput > MaxVariablesForOutput ) {
			tmpDDVariableTypes.allocate( MaxVariablesForOutput );
			tmpDDVariableTypes( {1,MaxVariablesForOutput} ) = DDVariableTypes( {1,MaxVariablesForOutput} );
			DDVariableTypes.deallocate();
			DDVariableTypes.allocate( MaxVariablesForOutput + LVarAllocInc );
			DDVariableTypes( {1,MaxVariablesForOutput} ) = tmpDDVariableTypes( {1,MaxVariablesForOutput} );
			tmpDDVariableTypes.deallocate();
			MaxVariablesForOutput += LVarAllocInc;
		}
		DDVariableTypes( NumVariablesForOutput ).IndexType = IndexType;
		DDVariableTypes( NumVariablesForOutput ).StoreType = StateType;
		DDVariableTypes( NumVariablesForOutput ).VariableType = VariableType;
		DDVariableTypes( NumVariablesForOutput ).VarNameOnly = VarName;
		DDVariableTypes( NumVariablesForOutput ).UnitsString = UnitsString;
	} else if ( UnitsString != DDVariableTypes( dup ).UnitsString ) { // not the same as first units
		dup2 = 0;
		while ( DDVariableTypes( dup ).Next != 0 ) {
			if ( UnitsString != DDVariableTypes( DDVariableTypes( dup ).Next ).UnitsString ) {
				dup = DDVariableTypes( dup ).Next;
				continue;
			}
			dup2 = DDVariableTypes( dup ).Next;
			break;
		}
		if ( dup2 == 0 ) {
			++NumVariablesForOutput;
			if ( NumVariablesForOutput > MaxVariablesForOutput ) {
				tmpDDVariableTypes.allocate( MaxVariablesForOutput );
				tmpDDVariableTypes( {1,MaxVariablesForOutput} ) = DDVariableTypes( {1,MaxVariablesForOutput} );
				DDVariableTypes.deallocate();
				DDVariableTypes.allocate( MaxVariablesForOutput + LVarAllocInc );
				DDVariableTypes( {1,MaxVariablesForOutput} ) = tmpDDVariableTypes( {1,MaxVariablesForOutput} );
				tmpDDVariableTypes.deallocate();
				MaxVariablesForOutput += LVarAllocInc;
			}
			DDVariableTypes( NumVariablesForOutput ).IndexType = IndexType;
			DDVariableTypes( NumVariablesForOutput ).StoreType = StateType;
			DDVariableTypes( NumVariablesForOutput ).VariableType = VariableType;
			DDVariableTypes( NumVariablesForOutput ).VarNameOnly = VarName;
			DDVariableTypes( NumVariablesForOutput ).UnitsString = UnitsString;
			DDVariableTypes( dup ).Next = NumVariablesForOutput;
		}
	}

}

//     NOTICE
//     Copyright � 1996-2014 The Board of Trustees of the University of Illinois
//     and The Regents of the University of California through Ernest Orlando Lawrence
//     Berkeley National Laboratory.  All rights reserved.
//     Portions of the EnergyPlus software package have been developed and copyrighted
//     by other individuals, companies and institutions.  These portions have been
//     incorporated into the EnergyPlus software package under license.   For a complete
//     list of contributors, see "Notice" located in EnergyPlus.f90.
//     NOTICE: The U.S. Government is granted for itself and others acting on its
//     behalf a paid-up, nonexclusive, irrevocable, worldwide license in this data to
//     reproduce, prepare derivative works, and perform publicly and display publicly.
//     Beginning five (5) years after permission to assert copyright is granted,
//     subject to two possible five year renewals, the U.S. Government is granted for
//     itself and others acting on its behalf a paid-up, non-exclusive, irrevocable
//     worldwide license in this data to reproduce, prepare derivative works,
//     distribute copies to the public, perform publicly and display publicly, and to
//     permit others to do so.
//     TRADEMARKS: EnergyPlus is a trademark of the US Department of Energy.


} // EnergyPlus