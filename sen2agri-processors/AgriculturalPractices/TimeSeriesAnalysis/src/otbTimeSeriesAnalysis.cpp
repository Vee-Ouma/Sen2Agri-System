/*=========================================================================
  *
  * Program:      Sen2agri-Processors
  * Language:     C++
  * Copyright:    2015-2016, CS Romania, office@c-s.ro
  * See COPYRIGHT file for details.
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.

 =========================================================================*/
#include "otbWrapperApplication.h"
#include "otbWrapperApplicationFactory.h"
#include <boost/filesystem.hpp>

#include "otbOGRDataSourceWrapper.h"

#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/fusion/algorithm/iteration/for_each.hpp>
#include <boost/accumulators/statistics.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include "TimeSeriesAnalysisTypes.h"
#include "TimeSeriesAnalysisUtils.h"
#include "StatisticsInfosReaderFactory.h"
#include "PracticeReaderFactory.h"

#define INPUT_SHP_DATE_PATTERN      "%4d-%2d-%2d"

#define CATCH_CROP_VAL                  "CatchCrop"
#define FALLOW_LAND_VAL                 "Fallow"
#define NITROGEN_FIXING_CROP_VAL        "NFC"


// TODO : re-analyse the usage of dates (start of week or exact date)

template <typename T>
bool InputFileLineInfoEqComparator (const InputFileLineInfoType& struct1, const InputFileLineInfoType& struct2) {
  return (struct1.weekNo == struct2.weekNo);
}

namespace otb
{
namespace Wrapper
{
class TimeSeriesAnalysis : public Application
{
public:
    /** Standard class typedefs. */
    typedef TimeSeriesAnalysis        Self;
    typedef Application                   Superclass;
    typedef itk::SmartPointer<Self>       Pointer;
    typedef itk::SmartPointer<const Self> ConstPointer;

    /** Standard macro */
    itkNewMacro(Self);

    itkTypeMacro(TimeSeriesAnalysis, otb::Application);

    /** Filters typedef */

private:
    TimeSeriesAnalysis()
    {
        m_countryName = "UNKNOWN";
        m_practiceName = "UNKNOWN";
        m_year = "UNKNOWN";

        m_OpticalThrVegCycle = 550; //350;

        // for MARKER 2 - NDVI loss
        // expected value of harvest/clearance
        m_NdviDown = 300;// 350;
        // buffer value (helps in case of sparse ndvi time-series)
        m_NdviUp = 400;//550;
        // opt.thr.value is round up to ndvi.step
        m_ndviStep = 5;
        m_OpticalThresholdMinimum = 100;

        // for MARKER 5 - COHERENCE increase
        m_CohThrBase = 0.1; //0.05;
        m_CohThrHigh = 0.2; //0.15;
        m_CohThrAbs = 0.7;  //0.75;

        // for MARKER 3 - BACKSCATTER loss
        m_AmpThrMinimum = 0.1;

        // INPUT THRESHOLDS - EFA PRACTICE evaluation
        // m_CatchMain = "InMainCrop";
        // TODO: Ask Gisat for CZE
        m_CatchPeriod = 56;               // in days (e.g. 8 weeks == 56 days)
        m_CatchProportion = 1/3;       // buffer threshold
        m_CatchPeriodStart = "2017-08-01";

        m_EfaNdviThr = 400; // 325;
        m_EfaNdviUp = 600; // 400;
        m_EfaNdviDown = 600; // 300;

        m_EfaCohChange = 0.2;
        m_EfaCohValue = 0.7;

        // TODO: ask Gisat about this value
        m_EfaNdviMin = 300;
        m_EfaAmpThr = NOT_AVAILABLE;

        m_UseStdDevInAmpThrValComp = false;
        m_OpticalThrBufDenominator = 6;

        m_CatchCropVal = CATCH_CROP_VAL;
        m_FallowLandVal = FALLOW_LAND_VAL;
        m_NitrogenFixingCropVal = NITROGEN_FIXING_CROP_VAL;

        // # optional: in case graphs shall be generated set the value to TRUE otherwise set to FALSE
        m_bPlotOutputGraph = false;
        // # optional: in case continuos products (csv file) shall be generated set the value to TRUE otherwise set to FALSE
        m_bResultContinuousProduct = false;
    }

    void DoInit() override
    {
        SetName("TimeSeriesAnalysis");
        SetDescription("TODO.");

        // Documentation
        SetDocName("TODO");
        SetDocLongDescription("TODO");
        SetDocLimitations("None");
        SetDocAuthors("CIU");
        SetDocSeeAlso(" ");

        AddDocTag(Tags::Learning);

        AddParameter(ParameterType_String, "dircohe", "Coherence statistics files directory");
        SetParameterDescription("dircohe", "Coherence statistics files directory");

        AddParameter(ParameterType_String, "diramp", "Amplitude statistics files directory");
        SetParameterDescription("diramp", "Amplitude statistics files directory");

        AddParameter(ParameterType_String, "dirndvi", "NDVI statistics files directory");
        SetParameterDescription("dirndvi", "NDVI statistics files directory");

        AddParameter(ParameterType_String, "harvestshp", "Harvest information shapefile");
        SetParameterDescription("harvestshp", "Harvest information shapefile");

        AddParameter(ParameterType_String, "outdir", "Output folder directory");
        SetParameterDescription("outdir", "Output folder directory");

        AddParameter(ParameterType_String, "intype", "Type of the inputs");
        SetParameterDescription("intype", "If xml then an xml file is expected as input. "
                                          "If dir, the application expects a directory with txt files for each parcel");
        MandatoryOff("intype");

        AddParameter(ParameterType_String, "country", "The country to be used in the output file name");
        SetParameterDescription("country", "The country to be used in the output file name");
        MandatoryOff("country");

        AddParameter(ParameterType_String, "practice", "The practice to be used in the output file name");
        SetParameterDescription("practice", "The practice to be used in the output file name");
        MandatoryOff("practice");

        AddParameter(ParameterType_String, "year", "The year to be used in the output file name");
        SetParameterDescription("practice", "The year to be used in the output file name");
        MandatoryOff("year");


// /////////////////////// CONFIGURABLE PARAMETERS PER SITE AND PRACTICE ////////////////////
        AddParameter(ParameterType_Float, "optthrvegcycle", "Optical threshold vegetation cycle");
        SetParameterDescription("optthrvegcycle", "Optical threshold vegetation cycle");
        SetDefaultParameterFloat("optthrvegcycle", 550);                                        // TODO: changing
        MandatoryOff("optthrvegcycle");

        // for MARKER 2 - NDVI loss
        // expected value of harvest/clearance
        AddParameter(ParameterType_Float, "ndvidw", "NDVI down");
        SetParameterDescription("ndvidw", "for MARKER 2 - NDVI loss - expected value of harvest/clearance");
        SetDefaultParameterFloat("ndvidw", 350);                                        // TODO: changing
        MandatoryOff("ndvidw");

        AddParameter(ParameterType_Float, "ndviup", "NDVI up");
        SetParameterDescription("ndviup", "buffer value (helps in case of sparse ndvi time-serie)");
        SetDefaultParameterFloat("ndviup", 550);                                        // TODO: changing
        MandatoryOff("ndviup");

        AddParameter(ParameterType_Float, "ndvistep", "NDVI step");
        SetParameterDescription("ndvistep", "opt.thr.value is round up to ndvi.step");
        SetDefaultParameterFloat("ndvistep", 5);
        MandatoryOff("ndvistep");

        AddParameter(ParameterType_Float, "optthrmin", "Optical threshold minimum");
        SetParameterDescription("optthrmin", "opt.thr.value is round up to ndvi.step");
        SetDefaultParameterFloat("optthrmin", 100);
        MandatoryOff("optthrmin");

        AddParameter(ParameterType_Float, "cohthrbase", "COHERENCE increase");
        SetParameterDescription("cohthrbase", "for MARKER 5 - COHERENCE increase");
        SetDefaultParameterFloat("cohthrbase", 0.05);                                   // TODO: changing
        MandatoryOff("cohthrbase");

        AddParameter(ParameterType_Float, "cohthrhigh", "Coherence threshold high");
        SetParameterDescription("cohthrhigh", "for MARKER 5 - COHERENCE increase");
        SetDefaultParameterFloat("cohthrhigh", 0.15);                                   // TODO: changing
        MandatoryOff("cohthrhigh");

        AddParameter(ParameterType_Float, "cohthrabs", "Coherence threshold absolute");
        SetParameterDescription("cohthrabs", "for MARKER 5 - COHERENCE increase");
        SetDefaultParameterFloat("cohthrabs", 0.75);                                   // TODO: changing
        MandatoryOff("cohthrabs");

        AddParameter(ParameterType_Float, "ampthrmin", "BACKSCATTER loss");
        SetParameterDescription("ampthrmin", "for MARKER 3 - BACKSCATTER loss");
        SetDefaultParameterFloat("ampthrmin", 0.1);                                   // TODO: changing
        MandatoryOff("ampthrmin");

        // INPUT THRESHOLDS - EFA PRACTICE evaluation
        AddParameter(ParameterType_String, "catchmain", "Catch main");
        SetParameterDescription("catchmain", "TODO");
        MandatoryOff("catchmain");

        AddParameter(ParameterType_Int, "catchperiod", "Catch period");
        SetParameterDescription("catchperiod", "in days (e.g. 8 weeks == 56 days)");
        SetDefaultParameterInt("catchperiod", 56);
        MandatoryOff("catchperiod");

        AddParameter(ParameterType_Float, "catchproportion", "Catch proportion");
        SetParameterDescription("catchproportion", "buffer threshold");
        SetDefaultParameterInt("catchproportion", 1/3);
        MandatoryOff("catchproportion");

        AddParameter(ParameterType_String, "catchperiodstart", "Catch period start");
        SetParameterDescription("catchperiodstart", "Catch period start");
        MandatoryOff("catchperiodstart");

        AddParameter(ParameterType_Int, "efandvithr", "Efa NDVI threshold");
        SetParameterDescription("efandvithr", "EFA practices NDVI threshold");
        SetDefaultParameterInt("efandvithr", 325);                // TODO: changing
        MandatoryOff("efandvithr");

        AddParameter(ParameterType_Int, "efandviup", "Efa NDVI up");
        SetParameterDescription("efandviup", "EFA practices NDVI up");
        SetDefaultParameterInt("efandviup", 400);                // TODO: changing
        MandatoryOff("efandviup");

        AddParameter(ParameterType_Int, "efandvidw", "Efa NDVI down");
        SetParameterDescription("efandvidw", "EFA practices NDVI down");
        SetDefaultParameterInt("efandvidw", 300);                // TODO: changing
        MandatoryOff("efandvidw");

        AddParameter(ParameterType_Float, "efacohchange", "Efa Coherence change");
        SetParameterDescription("efacohchange", "EFA practices coherence change");
        SetDefaultParameterInt("efacohchange", 0.2);
        MandatoryOff("efacohchange");

        AddParameter(ParameterType_Float, "efacohvalue", "Efa Coherence value");
        SetParameterDescription("efacohvalue", "EFA practices coherence value");
        SetDefaultParameterInt("efacohvalue", 0.7);
        MandatoryOff("efacohvalue");

        AddParameter(ParameterType_Float, "efandvimin", "Efa NDVI minimum");
        SetParameterDescription("efandvimin", "EFA practices NDVI minimum");
        SetDefaultParameterInt("efandvimin", 300);
        MandatoryOff("efandvimin");

        AddParameter(ParameterType_Float, "efaampthr", "Efa Amplitude threshold");
        SetParameterDescription("efaampthr", "EFA practices amplitude threshold");
        SetDefaultParameterInt("efaampthr", NOT_AVAILABLE);
        MandatoryOff("efaampthr");

        AddParameter(ParameterType_Int, "stddevinampthr", "Use Standard deviation in amplitude threshold value computation");
        SetParameterDescription("stddevinampthr", "Use Standard deviation in amplitude threshold value computation");
        SetDefaultParameterInt("stddevinampthr", 0);
        MandatoryOff("stddevinampthr");

        AddParameter(ParameterType_Int, "optthrbufden", "Optical threshold buffer denomimator");
        SetParameterDescription("optthrbufden", "Optical threshold buffer denomimator");
        SetDefaultParameterInt("optthrbufden", 6);
        MandatoryOff("optthrbufden");

        AddParameter(ParameterType_String, "flmarkstartdate", "Fallow land markers start date");
        SetParameterDescription("flmarkstartdate", "Fallow land markers start date");
        MandatoryOff("flmarkstartdate");

        AddParameter(ParameterType_String, "flmarkstenddate", "Fallow land markers end date");
        SetParameterDescription("flmarkstenddate", "Fallow land markers end date");
        MandatoryOff("flmarkstenddate");

        AddParameter(ParameterType_String, "catchcropval", "Catch crop value");
        SetParameterDescription("catchcropval", "Catch crop value");
        MandatoryOff("catchcropval");

        AddParameter(ParameterType_String, "flval", "Fallow land value");
        SetParameterDescription("flval", "Fallow land value");
        MandatoryOff("flval");

        AddParameter(ParameterType_String, "ncval", "Nitrogen fixing crop value");
        SetParameterDescription("ncval", "Nitrogen fixing crop value");
        MandatoryOff("ncval");

        AddParameter(ParameterType_Int, "plotgraph", "Plot output graphs");
        SetParameterDescription("plotgraph", "In case graphs shall be generated set the value to TRUE otherwise set to FALSE");
        SetDefaultParameterInt("plotgraph", 0);
        MandatoryOff("plotgraph");

        AddParameter(ParameterType_Int, "rescontprd", "Results continuous products");
        SetParameterDescription("rescontprd", "In case continuos products (csv file) shall be generated set the value to TRUE otherwise set to FALSE");
        SetDefaultParameterInt("rescontprd", 0);
        MandatoryOff("rescontprd");

        // Doc example parameter settings
        //SetDocExampleParameterValue("in", "support_image.tif");
    }

    void DoUpdateParameters() override
    {
    }

    void ExtractParameters() {
        m_outputDir = GetParameterAsString("outdir");
        if (HasValue("country")) {
            m_countryName = GetParameterAsString("country");
        }
        if (HasValue("practice")) {
            m_practiceName = GetParameterAsString("practice");
        }
        if (HasValue("year")) {
            m_year = GetParameterAsString("year");
        }
        if (HasValue("catchcropval")) {
            m_CatchCropVal = GetParameterString("catchcropval");
        }
        if (HasValue("flval")) {
            m_FallowLandVal = GetParameterString("flval");
        }
        if (HasValue("ncval")) {
            m_NitrogenFixingCropVal = GetParameterString("ncval");
        }

//  ///////////////////////////////////////////////////////////////////
        if (HasValue("optthrvegcycle")) {
            m_OpticalThrVegCycle = GetParameterFloat("optthrvegcycle");
        }
        if (HasValue("ndvidw")) {
            m_NdviDown = GetParameterFloat("ndvidw");
        }
        if (HasValue("ndviup")) {
            m_NdviUp = GetParameterFloat("ndviup");
        }
        if (HasValue("ndvistep")) {
            m_ndviStep = GetParameterFloat("ndvistep");
        }
        if (HasValue("optthrmin")) {
            m_OpticalThresholdMinimum = GetParameterFloat("optthrmin");
        }
        if (HasValue("cohthrbase")) {
            m_CohThrBase = GetParameterFloat("cohthrbase");
        }
        if (HasValue("cohthrhigh")) {
            m_CohThrHigh = GetParameterFloat("cohthrhigh");
        }
        if (HasValue("cohthrabs")) {
            m_CohThrAbs = GetParameterFloat("cohthrabs");
        }
        if (HasValue("ampthrmin")) {
            m_AmpThrMinimum = GetParameterFloat("ampthrmin");
        }
        if (HasValue("catchmain")) {
            m_CatchMain = GetParameterString("catchmain");
        }
        if (HasValue("catchperiod")) {
            m_CatchPeriod = GetParameterInt("catchperiod");
        }
        if (HasValue("catchproportion")) {
            m_CatchProportion = GetParameterFloat("catchproportion");
        }
        if (HasValue("catchperiodstart")) {
            m_CatchPeriodStart = GetParameterString("catchperiodstart");
        }
        if (HasValue("efandvithr")) {
            m_EfaNdviThr = GetParameterInt("efandvithr");
        }
        if (HasValue("efandviup")) {
            m_EfaNdviUp = GetParameterInt("efandviup");
        }
        if (HasValue("efandvidw")) {
            m_EfaNdviDown = GetParameterInt("efandvidw");
        }
        if (HasValue("efacohchange")) {
            m_EfaCohChange = GetParameterFloat("efacohchange");
        }
        if (HasValue("efacohvalue")) {
            m_EfaCohValue = GetParameterFloat("efacohvalue");
        }
        if (HasValue("efandvimin")) {
            m_EfaNdviMin = GetParameterFloat("efandvimin");
        }
        if (HasValue("efaampthr")) {
            m_EfaAmpThr = GetParameterFloat("efaampthr");
        }
        if (HasValue("stddevinampthr")) {
            m_UseStdDevInAmpThrValComp = GetParameterInt("stddevinampthr");
        }
        if (HasValue("optthrbufden")) {
            m_OpticalThrBufDenominator = GetParameterInt("optthrbufden");
        }
        if (HasValue("flmarkstartdate")) {
            m_flMarkersStartDateStr = GetParameterString("flmarkstartdate");
        }
        if (HasValue("flmarkstenddate")) {
            m_flMarkersEndDateStr = GetParameterString("flmarkstenddate");
        }
        if (HasValue("plotgraph")) {
            m_bPlotOutputGraph = GetParameterInt("plotgraph") != 0;
        }
        if (HasValue("rescontprd")) {
            m_bResultContinuousProduct = GetParameterInt("rescontprd") != 0;
        }

        if (m_practiceName == m_CatchCropVal) {
            if (m_CatchMain.size() == 0) {
                itkExceptionMacro("catch main parameter was not specified for the Catch Crop practice!");
            }
        }

        if (m_practiceName == m_FallowLandVal) {
            if (m_flMarkersEndDateStr.size() == 0) {
                itkExceptionMacro("Invalid or no value provided for parameter flmarkstenddate when Fallow practice used!");
            }
            if (m_flMarkersEndDateStr.size() == 0) {
                itkExceptionMacro("Invalid or no value provided for parameter flmarkstenddate when Fallow practice used!");
            }
        }

    }
    void DoExecute() override
    {
        // Extract the parameters
        ExtractParameters();

        std::string inputType = "xml";
        if (HasValue("intype")) {
            const std::string &inType = GetParameterAsString("intype");
            if (inType == "dir" || inType == "xml") {
                inputType = inType;
            } else {
                itkExceptionMacro("Invalid value provided for parameter intype " << inType
                                  << ". Only dir or xml are supported (default: xml)");
            }
        }

        auto factory = StatisticsInfosReaderFactory::New();
        m_pAmpReader = factory->GetInfosReader(inputType);
        m_pAmpReader->SetSource(GetParameterAsString("diramp"));
        m_pNdviReader = factory->GetInfosReader(inputType);
        m_pNdviReader->SetSource(GetParameterAsString("dirndvi"));
        m_pCoheReader = factory->GetInfosReader(inputType);
        m_pCoheReader->SetSource(GetParameterAsString("dircohe"));
        m_pCoheReader->SetMinRequiredEntries(26);

        const std::string &practicesInfoFile = GetParameterAsString("harvestshp");
        boost::filesystem::path practicesInfoPath(practicesInfoFile);
        std::string pfFormat = practicesInfoPath.extension().c_str();
        pfFormat.erase(pfFormat.begin(), std::find_if(pfFormat.begin(), pfFormat.end(), [](int ch) {
                return ch != '.';
            }));

        auto practiceReadersFactory = PracticeReaderFactory::New();
        m_pPracticeReader = practiceReadersFactory->GetPracticeReader(pfFormat);
        m_pPracticeReader->SetSource(practicesInfoFile);

        // write first the CSV header
        WriteCSVHeader(m_practiceName, m_countryName, std::atoi(m_year.c_str()));

        // create the plots file
        CreatePlotsFile(m_practiceName, m_countryName, std::atoi(m_year.c_str()));

        using namespace std::placeholders;
        std::function<bool(const FeatureDescription&)> f = std::bind(&TimeSeriesAnalysis::HandleFeature, this, _1);
        m_pPracticeReader->ExtractFeatures(f);

        // close the plots file
        ClosePlotsFile();
    }

    bool HandleFeature(const FeatureDescription& feature) {
        // DisplayFeature(feature);

        const std::string &fieldId = feature.GetFieldId();
        const std::string &vegetationStart = feature.GetVegetationStart();
        const std::string &harvestStart = feature.GetHarvestStart();
        const std::string &harvestEnd = feature.GetHarvestEnd();
        const std::string &practiceStart = feature.GetPracticeStart();
        const std::string &practiceEnd = feature.GetPracticeEnd();

        // TODO: Remove this after testing
//        if (fieldId != "681116204" && fieldId != "680105602" && fieldId != "584109910/2" && fieldId != "655115201_1")
//        {
//            return false;
//        }
        if (fieldId != "613102006_21")
        {
            return false;
        }


        FieldInfoType fieldInfos(fieldId);

        // TODO: the vegetationStart, harvestStart and harvestEnd are parsed twice.
        //       Change for only one parsing and use in the GetWeekFromDate the extracted year, month, day
        fieldInfos.ttVegStartTime = GetTimeFromString(vegetationStart);
        fieldInfos.ttVegStartWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttVegStartTime);
        fieldInfos.ttHarvestStartTime = GetTimeFromString(harvestStart);
        fieldInfos.ttHarvestStartWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttHarvestStartTime);
        fieldInfos.ttHarvestEndTime = GetTimeFromString(harvestEnd);
        fieldInfos.ttHarvestEndWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttHarvestEndTime);
        fieldInfos.ttPracticeStartTime = GetTimeFromString(practiceStart);
        fieldInfos.ttPracticeStartWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttPracticeStartTime);
        fieldInfos.ttPracticeEndTime = GetTimeFromString(practiceEnd);
        fieldInfos.ttPracticeEndWeekFloorTime = FloorDateToWeekStart(fieldInfos.ttPracticeEndTime);
        fieldInfos.practiceName = feature.GetPractice();
        fieldInfos.countryCode = feature.GetCountryCode();
        fieldInfos.mainCrop = feature.GetMainCrop();
        fieldInfos.practiceType = feature.GetPracticeType();

        int year;
        if (!GetWeekFromDate(vegetationStart, year, fieldInfos.vegStartWeekNo, INPUT_SHP_DATE_PATTERN))
        {
            otbAppLogWARNING("Cannot extract vegetation start week from the date " <<
                             vegetationStart << " of the feature " << fieldId);
            return false;
        }
        fieldInfos.year = year;
        if (!GetWeekFromDate(harvestStart, year, fieldInfos.harvestStartWeekNo, INPUT_SHP_DATE_PATTERN))
        {
            otbAppLogWARNING("Cannot extract harvest start week from the date " <<
                             harvestStart << " of the feature " << fieldId);
            return false;
        }
        if (year != fieldInfos.year) {
            otbAppLogWARNING("Vegetation year and harvest start year are different for field " << fieldId);
        }
        if (year != fieldInfos.year) {
            otbAppLogWARNING("Vegetation year and harvest start year are different for field " << fieldId);
        }

        if (fieldInfos.vegStartWeekNo == 1) {
            // increment the vegetation start time to the next week
            fieldInfos.ttVegStartWeekFloorTime += SEC_IN_WEEK;
            fieldInfos.vegStartWeekNo++;
        }
        if (fieldInfos.harvestStartWeekNo == 1) {
            // increment the harvest start time to the next week
            fieldInfos.ttVegStartWeekFloorTime += SEC_IN_WEEK;
            fieldInfos.harvestStartWeekNo++;
        }

//      DEBUG
        std::cout << "Field ID: " << fieldId << std::endl;
        std::cout << "Vegetation start: " << TimeToString(fieldInfos.ttVegStartTime) << std::endl;
        std::cout << "Vegetation start floor time: " << TimeToString(fieldInfos.ttVegStartWeekFloorTime) << std::endl;
        std::cout << "Vegetation start WEEK No: " <<  fieldInfos.vegStartWeekNo << std::endl;
        std::cout << "Harvest start: " << TimeToString(fieldInfos.ttHarvestStartTime) << std::endl;
        std::cout << "Harvest start floort time: " << TimeToString(fieldInfos.ttHarvestStartWeekFloorTime) << std::endl;
        std::cout << "Harvest start WEEK No: " <<  fieldInfos.harvestStartWeekNo << std::endl;
        std::cout << "Harvest end : " << TimeToString(fieldInfos.ttHarvestEndTime) << std::endl;
        std::cout << "Harvest end floor time: " << TimeToString(fieldInfos.ttHarvestEndWeekFloorTime) << std::endl;
//      DEBUG

        otbAppLogINFO("Extracting amplitude file infos for field  " << fieldId);
        if (!ExtractAmplitudeFilesInfos(fieldInfos)) {
            return false;
        }

        otbAppLogINFO("Extracting coherence file infos for field  " << fieldId);
        if (!ExtractCoherenceFilesInfos(fieldInfos)) {
            return false;
        }

        otbAppLogINFO("Extracting NDVI file infos for field  " << fieldId);
        if (!ExtractNdviFilesInfos(fieldInfos)) {
            return false;
        }

        otbAppLogINFO("Processing infos for field  " << fieldId);
        if (!ProcessFieldInformation(fieldInfos)) {
            return false;
        }

        return true;
    }

    bool ExtractAmplitudeFilesInfos(FieldInfoType &fieldInfo)
    {
        std::map<std::string, std::vector<InputFileLineInfoType>> mapInfos;
        if (!m_pAmpReader->GetEntriesForField(fieldInfo.fieldId, {"VV", "VH"}, mapInfos)) {
            otbAppLogWARNING("Cannot extract amplitude infos for the field " << fieldInfo.fieldId);
            return false;
        }
        std::map<std::string, std::vector<InputFileLineInfoType>>::const_iterator itVV;
        std::map<std::string, std::vector<InputFileLineInfoType>>::const_iterator itVH;
        itVV = mapInfos.find("VV");
        itVH = mapInfos.find("VH");
        if (itVV == mapInfos.end() || itVH == mapInfos.end() ||
                itVV->second.size() == 0 || itVH->second.size() == 0) {
            otbAppLogWARNING("Didn't find entries for both VV and VH amplitude files for the field " << fieldInfo.fieldId);
            return false;
        }
        fieldInfo.ampVVLines.insert(fieldInfo.ampVVLines.end(), itVV->second.begin(), itVV->second.end());
        fieldInfo.ampVHLines.insert(fieldInfo.ampVHLines.end(), itVH->second.begin(), itVH->second.end());

        if (!CheckWeekGaps(fieldInfo.vegStartWeekNo, fieldInfo.ampVVLines) ||
            !CheckWeekGaps(fieldInfo.vegStartWeekNo, fieldInfo.ampVHLines)) {
            otbAppLogWARNING("No valid amplitude lines were found for the field id " << fieldInfo.fieldId);
            return false;
        }


        return true;
    }

    bool ExtractCoherenceFilesInfos(FieldInfoType &fieldInfo)
    {
        std::map<std::string, std::vector<InputFileLineInfoType>> mapInfos;
        if (!m_pCoheReader->GetEntriesForField(fieldInfo.fieldId, {"VV", "VH"}, mapInfos)) {
            otbAppLogWARNING("Cannot extract coherence infos for the field " << fieldInfo.fieldId);
            return false;
        }
        std::map<std::string, std::vector<InputFileLineInfoType>>::const_iterator itVV;
        std::map<std::string, std::vector<InputFileLineInfoType>>::const_iterator itVH;
        itVV = mapInfos.find("VV");
        itVH = mapInfos.find("VH");
        if (itVV == mapInfos.end() || itVH == mapInfos.end() ||
            itVV->second.size() == 0 || itVH->second.size() == 0) {
            otbAppLogWARNING("Didn't find entries for both VV and VH coherence files for the field " << fieldInfo.fieldId);
            return false;
        }
        fieldInfo.coheVVLines.insert(fieldInfo.coheVVLines.end(), itVV->second.begin(), itVV->second.end());
        fieldInfo.coheVHLines.insert(fieldInfo.coheVHLines.end(), itVH->second.begin(), itVH->second.end());
        if (fieldInfo.coheVVLines.size() <= 25 || fieldInfo.coheVHLines.size() <= 25) {
            otbAppLogWARNING("No/empty/short coherence input text files the field id " << fieldInfo.fieldId);
            return false;
        }
        for (std::vector<InputFileLineInfoType>::const_iterator it = fieldInfo.coheVVLines.begin() ;
             it != fieldInfo.coheVVLines.end(); ++it) {
            if (fieldInfo.coheVVMaxValue == NOT_AVAILABLE) {
                fieldInfo.coheVVMaxValue = it->meanVal;
            } else {
                if (fieldInfo.coheVVMaxValue < it->meanVal) {
                    fieldInfo.coheVVMaxValue = it->meanVal;
                }
            }
        }

        if (!CheckWeekGaps(fieldInfo.vegStartWeekNo, fieldInfo.coheVVLines) ||
            !CheckWeekGaps(fieldInfo.vegStartWeekNo, fieldInfo.coheVHLines)) {
            otbAppLogWARNING("No valid coherence lines were found for the field id " << fieldInfo.fieldId);
            return false;
        }

        return true;
    }

    bool ExtractNdviFilesInfos(FieldInfoType &fieldInfo)
    {
        if (!m_pNdviReader->GetEntriesForField(fieldInfo.fieldId, fieldInfo.ndviLines)) {
            otbAppLogWARNING("Cannot extract NDVI infos for the field " << fieldInfo.fieldId);
            return false;
        }
        if (fieldInfo.ndviLines.size() == 0) {
            otbAppLogWARNING("No valid lines were extracted for the field id " << fieldInfo.fieldId);
        }
        return true;
    }

    bool CheckWeekGaps(int &vegetationStartWeek, const std::vector<InputFileLineInfoType> &lineInfos)
    {
        if (lineInfos.size() == 0) {
            return false;
        }

        bool gaps = false;
        int validLines = 0;
        int prevWeekNo = -1;

        typename std::vector<InputFileLineInfoType>::const_iterator infosIter;
        for (infosIter = lineInfos.begin(); infosIter != lineInfos.end(); ++infosIter)
        {
            if (vegetationStartWeek <= infosIter->weekNo) {
                // we found a valid line
                if (prevWeekNo == -1) {
                    prevWeekNo = infosIter->weekNo;
                    continue;
                }
                if (infosIter->weekNo - prevWeekNo > 1) {
                    gaps = true;
                    break;
                }
                prevWeekNo = infosIter->weekNo;
                // we ignore the first valid line
                validLines++;
            }
        }
        if (gaps || validLines == 0) {
            return false;
        }

        return true;
    }

    bool ProcessFieldInformation(FieldInfoType &fieldInfos) {
        // Read VV and VH backscatter (grd) and compute backscatter ratio (VV-VH)
        otbAppLogINFO("Merging amplitudes for field  " << fieldInfos.fieldId);
        if (!MergeAmplitudes(fieldInfos.fieldId, fieldInfos.ampVVLines, fieldInfos.ampVHLines, fieldInfos.mergedAmpInfos)) {
            return false;
        }

        // DEBUG
//        PrintAmplitudeInfos(fieldInfos);
        // DEBUG

        otbAppLogINFO("Grouping amplitudes by weeks for field  " << fieldInfos.fieldId);
        // Group backscatter ratio by weeks (7-day period) - compute week-mean ($grd.mean)
        if (!GroupTimedValuesByWeeks(fieldInfos.mergedAmpInfos, fieldInfos.ampRatioGroups)) {
            return false;
        }

        // DEBUG
        //PrintAmpGroupedMeanValues(fieldInfos.ampRatioGroups);
        // DEBUG

        otbAppLogINFO("Computing amplitude 3 weeks mean - current value mean for field  " << fieldInfos.fieldId);
        // Compute 3-weeks-mean before a week ($grd.w3m)
        // and compute difference between week-mean and previous 3-week-mean ($grd.change)
        if (!Compute3WeeksAmplRatioDiffs(fieldInfos.ampRatioGroups)) {
            return false;
        }

        // DEBUG
//        PrintAmpGroupedMeanValues(fieldInfos.ampRatioGroups);
        // DEBUG

        // DEBUG
//        PrintNdviInfos(fieldInfos);
        // DEBUG

        otbAppLogINFO("Grouping NDVI by weeks for field  " << fieldInfos.fieldId);
        if (!GroupTimedValuesByWeeks(fieldInfos.ndviLines, fieldInfos.ndviGroups)) {
            return false;
        }

        // round the values from the NDVI groupes
        std::transform(fieldInfos.ndviGroups.begin(), fieldInfos.ndviGroups.end(), fieldInfos.ndviGroups.begin(),
                       [](GroupedMeanValInfosType &x) {
                x.meanVal = std::round(x.meanVal);
                return x;
        });

        // DEBUG
//        PrintNdviGroupedMeanValues(fieldInfos.ndviGroups);
        // DEBUG

        // DEBUG
//        PrintCoherenceInfos(fieldInfos);
        // DEBUG

        otbAppLogINFO("Grouping Coherence by weeks for field  " << fieldInfos.fieldId);
        if (!GroupTimedValuesByWeeks(fieldInfos.coheVVLines, fieldInfos.coherenceGroups)) {
            return false;
        }

        // round the coherence change values
        // round the values from the NDVI groupes
        std::transform(fieldInfos.coherenceGroups.begin(), fieldInfos.coherenceGroups.end(), fieldInfos.coherenceGroups.begin(),
                       [](GroupedMeanValInfosType &x) {
                x.maxChangeVal = (std::round(x.maxChangeVal * 1000) / 1000);
                return x;
        });

        // DEBUG
//        PrintCoherenceGroupedMeanValues(fieldInfos.coherenceGroups);
        // DEBUG

        otbAppLogINFO("Merging all information for field  " << fieldInfos.fieldId);
        std::vector<MergedAllValInfosType> allMergedValues;
        if (!MergeAllFieldInfos(fieldInfos, fieldInfos.ampRatioGroups, fieldInfos.coherenceGroups,
                                fieldInfos.ndviGroups, allMergedValues)) {
            return false;
        }

        // DEBUG
//        PrintMergedValues(allMergedValues);
        // DEBUG

        UpdateMarker1Infos(fieldInfos, allMergedValues);

        // DEBUG
//        PrintMergedValues(allMergedValues);
        // DEBUG

        UpdateMarker2Infos(fieldInfos, allMergedValues);

        // DEBUG
//        PrintMergedValues(allMergedValues);
        // DEBUG

        UpdateMarker5Infos(fieldInfos, allMergedValues);

        // DEBUG
//        PrintMergedValues(allMergedValues);
        // DEBUG

        double ampThrValue;
        UpdateMarker3Infos(fieldInfos, allMergedValues, ampThrValue);

        // DEBUG
//        PrintMergedValues(allMergedValues);
        // DEBUG

        std::cout << "Amplitude threshold value is: " << ValueToString(ampThrValue) << std::endl;

        UpdateMarker4Infos(fieldInfos, allMergedValues, ampThrValue);

        // DEBUG
        PrintMergedValues(allMergedValues);
        // DEBUG

        HarvestInfoType harvestInfos;
        HarvestEvaluation(fieldInfos, allMergedValues, harvestInfos);

//      DEBUG
        PrintHarvestEvaluation(fieldInfos, harvestInfos);
//      DEBUG

        HarvestInfoType efaHarvestInfos;

        // ### TIME SERIES ANALYSIS FOR EFA PRACTICES ###
        if (fieldInfos.practiceName != "NA" && fieldInfos.ttPracticeStartTime != 0) {
            if (fieldInfos.practiceName == m_CatchCropVal) {
                CatchCropPracticeAnalysis(fieldInfos, allMergedValues, harvestInfos, efaHarvestInfos);
            } else if (fieldInfos.practiceName == m_FallowLandVal) {
                FallowLandPracticeAnalysis(fieldInfos, allMergedValues, harvestInfos, efaHarvestInfos);
            } else if (fieldInfos.practiceName == m_NitrogenFixingCropVal) {
                NitrogenFixingCropPracticeAnalysis(fieldInfos, allMergedValues, harvestInfos, efaHarvestInfos);
            } else {
                otbAppLogWARNING("Practice name " << fieldInfos.practiceName << " not supported!");
            }
        }

        if (m_bPlotOutputGraph) {
            // write infos to be generated as plots (png graphs)
            WritePlotEntry(fieldInfos, harvestInfos);
        }
        if (m_bResultContinuousProduct) {
            // TODO:
//            continuous.product <- subset(group.merge, select=c(Group.1,ndvi.presence,candidate.opt,candidate.grd,grd.presence,candidate.coh) )
//            colnames(continuous.product) <- c("WEEK","M1","M2","M3","M4","M5")
//            continuous.product$WEEK <- week(continuous.product$WEEK)
//            write.table(continuous.product,file=paste(result.continuous.folder,result.continuous.file,strtrim(field,nchar(field)-1),".csv",sep=""),sep=";",append=FALSE,row.names=FALSE,col.names=TRUE,quote=FALSE)
        }

        WriteHarvestInfoToCsv(fieldInfos, harvestInfos, efaHarvestInfos);

        return true;
    }

    bool MergeAmplitudes(const std::string &fieldId, const std::vector<InputFileLineInfoType> &ampVVLines,
                         const std::vector<InputFileLineInfoType> &ampVHLines,
                         std::vector<MergedDateAmplitudeType> &retInfos) {
        if (ampVVLines.size() == 0) {
            otbAppLogWARNING("The amplitude file info sizes is zero for field " << fieldId);
            return false;
        }
        if (ampVVLines.size() != ampVHLines.size()) {
            otbAppLogWARNING("The extracted amplitude file sizes differ for field " << fieldId);
            return false;
        }
        retInfos.resize(ampVVLines.size());
        // Theoretically we should have at the same position the same date
        for (size_t i = 0; i<ampVVLines.size(); i++) {
            const InputFileLineInfoType &vvLine = ampVVLines[i];
            const InputFileLineInfoType &vhLine = ampVHLines[i];
            if (vvLine.ttDate != vhLine.ttDate) {
                otbAppLogWARNING("Date inconsistency detected for amplitudes at index " << i << " for field " << fieldId);
                return false;
            }
            retInfos[i].vvInfo = vvLine;
            retInfos[i].vhInfo = vhLine;
            retInfos[i].ampRatio = vvLine.meanVal - vhLine.meanVal;
            retInfos[i].ttDate = vvLine.ttDate;
        }
        return true;
    }

    template <typename TimedValue>
    bool GroupTimedValuesByWeeks(const std::vector<TimedValue> &infos,
                                std::vector<GroupedMeanValInfosType> &retGroups) {
        double curMaxVal;
        double curMaxChangeVal;
        double curVal;
        double curChangeVal;
        time_t ttTime;
        std::map<time_t, GroupedMeanValInfosType> mapGroups;
        for (const auto &info: infos) {
            ttTime = info.GetFloorTime();
            curMaxVal = mapGroups[ttTime].maxVal;
            curVal = info.GetValue();
            mapGroups[ttTime].ttDate = ttTime;
            mapGroups[ttTime].maxVal = (curMaxVal < curVal) ? curVal : curMaxVal ;
            mapGroups[ttTime].sum += curVal;
            mapGroups[ttTime].cnt++;
            mapGroups[ttTime].meanVal = (mapGroups[ttTime].sum / mapGroups[ttTime].cnt);

            // Get the change value, if supported
            if (info.GetChangeValue(curChangeVal)) {
                curMaxChangeVal = mapGroups[ttTime].maxChangeVal;
                mapGroups[ttTime].maxChangeVal = (curMaxChangeVal < curChangeVal) ? curChangeVal : curMaxChangeVal ;
            }
        }

        for( std::map<time_t, GroupedMeanValInfosType>::iterator it = mapGroups.begin(); it != mapGroups.end(); ++it ) {
                retGroups.push_back( it->second );
        }
        std::sort(retGroups.begin(), retGroups.end(), GroupedMeanValInfosComparator());

        return true;
    }

    // Compute 3-weeks-mean before a week and then
    // Compute difference between week-mean and previous 3-week-mean
    bool Compute3WeeksAmplRatioDiffs(std::vector<GroupedMeanValInfosType> &ampRatioGroups) {
        double cur3WeeksMeanVal;
        for (size_t i = 0; i<ampRatioGroups.size(); i++)
        {
            if (i >= 3) {
                // Compute 3-weeks-mean before a week ($grd.w3m)
                cur3WeeksMeanVal = 0;
                for (int j = 1; j<=3; j++) {
                    cur3WeeksMeanVal += ampRatioGroups[i - j].meanVal;
                }
                cur3WeeksMeanVal /= 3;
                // Compute difference between week-mean and previous 3-week-mean
                ampRatioGroups[i].ampChange = ampRatioGroups[i].meanVal - cur3WeeksMeanVal;
                if (ampRatioGroups[i].ampChange < 0) {
                    ampRatioGroups[i].ampChange = 0;
                } else {
                    // round the value to 2 decimals
                    ampRatioGroups[i].ampChange = std::round(ampRatioGroups[i].ampChange * 100)/100;
                }
            }
        }

        return true;
    }

    bool MergeAllFieldInfos(const FieldInfoType &fieldInfos, const std::vector<GroupedMeanValInfosType> &ampRatioGroups,
                            const std::vector<GroupedMeanValInfosType> &coherenceGroups,
                            const std::vector<GroupedMeanValInfosType> &ndviGroups,
                            std::vector<MergedAllValInfosType> &retAllMergedValues)
    {
        if (ampRatioGroups.size() != coherenceGroups.size()) {
            otbAppLogWARNING("Amplitude and coherence groups sizes differ when merging for field " << fieldInfos.fieldId);
        }

        size_t retListSize = ampRatioGroups.size() > coherenceGroups.size() ? coherenceGroups.size() : ampRatioGroups.size();
        retAllMergedValues.resize(retListSize);

        // fill the amplitude fields and other fields to NA
        for(size_t i = 0; i < retListSize; i++) {
            retAllMergedValues[i].ttDate = ampRatioGroups[i].ttDate;

            retAllMergedValues[i].ampMean = ampRatioGroups[i].meanVal;
            retAllMergedValues[i].ampMax = ampRatioGroups[i].maxVal;
            retAllMergedValues[i].ampChange = ampRatioGroups[i].ampChange;

            // Fill the coherence fields
            // gaps are not expected (after start of vegetation period) - tested in tables_check
            retAllMergedValues[i].cohChange = coherenceGroups[i].maxChangeVal;
            retAllMergedValues[i].cohMax = coherenceGroups[i].maxVal;

            // Set all these values to NOT_AVAILABLE
            retAllMergedValues[i].ndviMeanVal = NOT_AVAILABLE;
            retAllMergedValues[i].ndviPrev = NOT_AVAILABLE;
            retAllMergedValues[i].ndviNext = NOT_AVAILABLE;
            retAllMergedValues[i].ndviPresence = NOT_AVAILABLE;
            retAllMergedValues[i].ndviDrop = NOT_AVAILABLE;
            retAllMergedValues[i].vegWeeks = NOT_AVAILABLE;
            retAllMergedValues[i].candidateOptical = false;
        }

        // Fill the NDVI fields
        // In this case, we might have gaps
        double prevNdviVal = NOT_AVAILABLE;
        for(size_t i = 0; i<retListSize; i++) {
            bool valueSet = false;
            for(size_t j = 0; j<ndviGroups.size(); j++) {
                if(retAllMergedValues[i].ttDate == ndviGroups[j].ttDate) {
                    retAllMergedValues[i].ndviMeanVal = ndviGroups[j].meanVal;
                    valueSet = true;
                    break;
                }
            }
            // fill the NDVI prev values
            if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime) {
                if (valueSet) {
                    if (IsNA(prevNdviVal)) {
                        // set the current value if NA previously
                        retAllMergedValues[i].ndviPrev = retAllMergedValues[i].ndviMeanVal;
                    } else {
                        // in this case, if exists a prevVal, set it
                        retAllMergedValues[i].ndviPrev = prevNdviVal;
                    }
                    // save current value the previously var
                    prevNdviVal = retAllMergedValues[i].ndviMeanVal;
                } else {
                    // just keep the previous value, whatever it is
                    retAllMergedValues[i].ndviPrev = prevNdviVal;
                }
            }
        }

        // fill the NDVI next values
        double nextNdviVal = NOT_AVAILABLE;
        // iterate backwards for filling the next values
        for (int i = (int)retListSize-1; i>=1; i--) {
            if (retAllMergedValues[i].ttDate < fieldInfos.ttVegStartWeekFloorTime) {
                // Normally, there is no need to continue
                break;
            }
            if (!IsNA(retAllMergedValues[i].ndviMeanVal)) {
                nextNdviVal = retAllMergedValues[i].ndviMeanVal;
                retAllMergedValues[i].ndviNext = nextNdviVal;
            }
            retAllMergedValues[i-1].ndviNext = nextNdviVal;
        }

        for(size_t i = 0; i<retListSize; i++) {
            if (!IsNA(retAllMergedValues[i].ndviMeanVal) && IsNA(retAllMergedValues[i].ndviPrev) && !IsNA(retAllMergedValues[i].ndviNext)) {
                retAllMergedValues[i].ndviPrev = retAllMergedValues[i].ndviNext;
            }
        }
        return true;
    }

    template <typename Value>
    std::vector<Value> FilterValuesByDates(const std::vector<Value> &mergedValues,
                                                      time_t ttStartTime, time_t ttEndTime) const {
        // if no filter, return the initial vector
        int vecSize = mergedValues.size();
        if ((ttStartTime == 0 && ttEndTime == 0) || (vecSize == 0)) {
            return mergedValues;
        }
        // assuming they are sorted, check the first and the last
        if (ttStartTime <= mergedValues[0].ttDate &&
                ttEndTime >= mergedValues[vecSize-1].ttDate) {
            return mergedValues;
        }
        // we need to filter values
        std::vector<Value> retVect;
        for (typename std::vector<Value>::const_iterator it = mergedValues.begin(); it != mergedValues.end(); ++it) {
            if(it->ttDate >= ttStartTime && it->ttDate <= ttEndTime) {
                retVect.push_back(*it);
            }
        }
        return retVect;
    }


    void UpdateMarker1Infos(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues) {
        // MARKER 1  Presence of vegetation cycle (NDVI): M1 == $ndvi.presence
        // Define weeks in vegetation season
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            // TODO: Check the condition fieldInfos.ttPracticeStartTime != 0
            // TODO: ask to Gisat why + 7
            // TODO: Ask gisat if is OK date.a + 7, where date.a = P_START. Shouldn't be date.b = P_END?
            if (fieldInfos.practiceName == "CatchCrop" && fieldInfos.ttPracticeStartTime != 0) {
                if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime &&
                    retAllMergedValues[i].ttDate <= fieldInfos.ttPracticeStartWeekFloorTime + SEC_IN_WEEK) {
                    retAllMergedValues[i].vegWeeks = true;
                }
            } else {
                if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime &&
                    retAllMergedValues[i].ttDate <= fieldInfos.ttHarvestEndTime) {
                    retAllMergedValues[i].vegWeeks = true;
                }
            }
        }

        // Define presence of NDVI (M1) for vegetation season
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            if (retAllMergedValues[i].ttDate >= fieldInfos.ttVegStartWeekFloorTime &&
                retAllMergedValues[i].ttDate <= fieldInfos.ttHarvestEndWeekFloorTime) {
                if (IsNA(retAllMergedValues[i].ndviMeanVal)) {
                    retAllMergedValues[i].ndviPresence = false;
                } else if (retAllMergedValues[i].ndviMeanVal > m_OpticalThrVegCycle) {
                    // here we must set all the values from i to the date of Harvest End to true
                    for (int j = i; retAllMergedValues[j].ttDate <= fieldInfos.ttHarvestEndWeekFloorTime; j++) {
                        retAllMergedValues[j].ndviPresence = true;
                    }
                    break;  // exit now, all needed entries were processed
                } else {
                    retAllMergedValues[i].ndviPresence = false;
                }
            }
        }
    }

    void UpdateMarker2Infos(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues) {
        double prevValidNdviMeanVal = NOT_AVAILABLE;

        bool allVwstNdviDw = true; // all vegetation weeks smaller than NDVI down
        bool ndviDropMeanLessDw = false; // at least one NDVI mean value corresponding to a drop is less than NDVI down
        bool ndviDropMeanLessUp = false; // at least one NDVI mean value corresponding to a drop is less than NDVI up
        double minMeanNdviDropVal = NOT_AVAILABLE;
        double maxMeanNdviDropVal = NOT_AVAILABLE;
        int nVegWeeksCnt = 0;
        bool bHasNdviDrop = false;

        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            retAllMergedValues[i].ndviDrop = NOT_AVAILABLE;
            if (IsNA(retAllMergedValues[i].ndviMeanVal)) {
                continue;
            }
            if (retAllMergedValues[i].vegWeeks == true) {
                nVegWeeksCnt++;

                if ((retAllMergedValues[i].ndviPresence == true) && !IsNA(prevValidNdviMeanVal)) {
                    // update the NDVI drop only to the ones that have NDVI presence true
                    retAllMergedValues[i].ndviDrop = ((retAllMergedValues[i].ndviMeanVal < prevValidNdviMeanVal) &&
                            (retAllMergedValues[i].ndviMeanVal < m_NdviUp));

                    // compute additional infos for computing opt.thr.value
                    if (retAllMergedValues[i].ndviDrop) {
                        bHasNdviDrop = true;
                        if (retAllMergedValues[i].ndviMeanVal < m_NdviDown) {
                            ndviDropMeanLessDw = true;
                        }
                        if (retAllMergedValues[i].ndviMeanVal < m_NdviUp) {
                            ndviDropMeanLessUp = true;
                        }
                        if (minMeanNdviDropVal == NOT_AVAILABLE) {
                            minMeanNdviDropVal = retAllMergedValues[i].ndviMeanVal;
                        } else {
                            if (minMeanNdviDropVal > retAllMergedValues[i].ndviMeanVal) {
                                minMeanNdviDropVal = retAllMergedValues[i].ndviMeanVal;
                            }
                        }
                    }
                }
                // save the previous valid ndvi mean value that has veg.weeks = true needed for ndvi.drop calculation
                prevValidNdviMeanVal = retAllMergedValues[i].ndviMeanVal;

                // compute the maximum ndvi drop needed for opt.thr.value computation
                if (retAllMergedValues[i].ndviMeanVal >= m_NdviDown) {
                    allVwstNdviDw = false;
                }
                if (maxMeanNdviDropVal == NOT_AVAILABLE) {
                    maxMeanNdviDropVal = retAllMergedValues[i].ndviMeanVal;
                } else {
                    if (maxMeanNdviDropVal < retAllMergedValues[i].ndviMeanVal) {
                        maxMeanNdviDropVal = retAllMergedValues[i].ndviMeanVal;
                    }
                }
            }
//          DEBUG
//            std::cout<< i+1 << " " << ValueToString(retAllMergedValues[i].ndviDrop, true) << std::endl;
//          DEBUG
        }

        // Compute the opt.thr.value
        double OpticalThresholdValue;
        if (allVwstNdviDw || ndviDropMeanLessDw) {
            OpticalThresholdValue = m_NdviDown;
        } else if (ndviDropMeanLessUp) {
            OpticalThresholdValue = m_ndviStep * std::ceil(minMeanNdviDropVal / m_ndviStep);
        } else {
            OpticalThresholdValue = m_NdviUp;
        }

        double OpticalThresholdBuffer = ComputeOpticalThresholdBuffer(nVegWeeksCnt, maxMeanNdviDropVal, OpticalThresholdValue);

        // Define start of harvest period based on optical data (harvest.opt.start)
        time_t harvestOpticalStart = 0;
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            // TODO: Ask Gisat why condition :
            // group.merge$ndvi.next<opt.thr.value | group.merge$ndvi.next<opt.thr.value+opt.thr.buffer
            if (((retAllMergedValues[i].ndviNext < OpticalThresholdValue) ||
                 (retAllMergedValues[i].ndviNext < OpticalThresholdValue + OpticalThresholdBuffer)) &&
                    (retAllMergedValues[i].ndviPrev > OpticalThresholdValue) &&
                    (retAllMergedValues[i].ndviPrev > retAllMergedValues[i].ndviNext) &&
                    (retAllMergedValues[i].ttDate >= fieldInfos.ttHarvestStartWeekFloorTime)) {
                harvestOpticalStart = retAllMergedValues[i].ttDate;
                break;
            }
        }
        if (harvestOpticalStart == 0) {
            harvestOpticalStart = fieldInfos.ttHarvestStartWeekFloorTime;
        }
//          DEBUG
//        std::cout << TimeToString(fieldInfos.ttHarvestStartWeekFloorTime) << std::endl;
//        std::cout << TimeToString(harvestOpticalStart) << std::endl;
//          DEBUG

        // Define candidate-weeks for harvest based on optical data (M2)
        if (bHasNdviDrop) {
            for(size_t i = 0; i<retAllMergedValues.size(); i++) {
                if (!IsNA(retAllMergedValues[i].ndviNext) && !IsNA(retAllMergedValues[i].ndviPrev)) {
                    if (((retAllMergedValues[i].ndviNext < OpticalThresholdValue) ||
                         (retAllMergedValues[i].ndviNext < OpticalThresholdValue + OpticalThresholdBuffer) ||
                         (retAllMergedValues[i].ndviPrev < OpticalThresholdValue) ||
                         (retAllMergedValues[i].ndviPrev < OpticalThresholdValue + OpticalThresholdBuffer)) &&
                            (retAllMergedValues[i].ndviPrev > m_OpticalThresholdMinimum) &&
                            (retAllMergedValues[i].ttDate >= harvestOpticalStart) &&
                            ((retAllMergedValues[i].ndviPrev + m_NdviDown) > retAllMergedValues[i].ndviNext)) {
                        retAllMergedValues[i].candidateOptical = true;
                    }
                }
            }
        }
    }


    void UpdateMarker3Infos(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                        double &ampThrValue) {
        std::vector<int> ampOutlier;
        std::map<time_t, int> mapCnts;
        std::map<time_t, double> mapSums;
        ampOutlier.resize(fieldInfos.ampVVLines.size());
        for(size_t i = 0; i < fieldInfos.ampVVLines.size(); i++) {
            // for backscatter outliars VV < -3 dB  (draft soulution)
            ampOutlier[i] = (fieldInfos.ampVVLines[i].meanVal > -3) ? 1 : 0;
        }
        // first extract the sum and counts for each date
        for(size_t i = 0; i < fieldInfos.ampVVLines.size(); i++) {
            if (ampOutlier[i] == 0) {
                mapCnts[fieldInfos.ampVVLines[i].ttDateFloor]++;
                mapSums[fieldInfos.ampVVLines[i].ttDateFloor] += fieldInfos.mergedAmpInfos[i].ampRatio;
            }
        }
        // Compute the mean for the filtered amplitudes
        std::vector<double> meanAmpValsFiltered;
        for( std::map<time_t, double>::iterator it = mapSums.begin(); it != mapSums.end(); ++it ) {
            int cntVal = mapCnts[it->first];
            if (cntVal > 0) {
                meanAmpValsFiltered.push_back(it->second / cntVal);
            }
        }
//          DEBUG
//        for (int i = 0; i<meanAmpValsFiltered.size(); i++) {
//            std::cout << meanAmpValsFiltered[i] << std::endl;
//        }
//          DEBUG
        // Compute the standard deviation
        double meanVal = 0;
        double stdDevVal = 0;
        // Compute Sample Standard Deviation instead of Population Standard Deviation
        // TODO: To be confirmed by Gisat
        ComputeMeanAndStandardDeviation(meanAmpValsFiltered, meanVal, stdDevVal, true);

//        grd.thr.break <- paste("grd.thr.break <- sd(grd.filtered)/6")
//        grd.thr.value <- paste("grd.thr.value <- mean(grd.filtered)-sd(grd.filtered)/2")
        double ampThrBreak = ComputeAmplitudeThresholdBreak(stdDevVal);
        ampThrValue = ComputeAmplitudeThresholdValue(meanVal, stdDevVal);
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            retAllMergedValues[i].candidateAmplitude = (retAllMergedValues[i].ampChange > ampThrBreak);
//          DEBUG
//            std::cout << ValueToString(retAllMergedValues[i].candidateAmplitude, true) << std::endl;
//          DEBUG
        }

        // we iterate from index 1 to index size - 2 but we access also these indexes
        for(size_t i = 1; i<retAllMergedValues.size()-1; i++) {
            if ((retAllMergedValues[i].cohChange > m_CohThrHigh) &&
                    ((retAllMergedValues[i-1].candidateAmplitude) ||
                     (retAllMergedValues[i].candidateAmplitude) ||
                     (retAllMergedValues[i+1].candidateAmplitude))) {
                retAllMergedValues[i].candidateAmplitude = true;
            }
        }
    }


    void UpdateMarker4Infos(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                            double &ampThrValue) {
        (void)fieldInfos;    // supress unused warning
        // MARKER 4  Presence of vegetation cycle (BACKSCATTER): M4 == $grd.presence
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            retAllMergedValues[i].amplitudePresence = (retAllMergedValues[i].ampMax > ampThrValue);
            if (retAllMergedValues[i].cohChange > m_CohThrHigh &&
                    retAllMergedValues[i].ndviPresence &&
                    retAllMergedValues[i].candidateOptical &&
                    retAllMergedValues[i].candidateAmplitude &&
                    !retAllMergedValues[i].amplitudePresence ) {
                retAllMergedValues[i].amplitudePresence = TRUE;
            }
        }
    }


    void UpdateMarker5Infos(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues) {
         // Define candidate-weeks for harvest based on coherence data (M5)
        double CohThrAbs = m_CohThrAbs;
        if (fieldInfos.coheVVMaxValue < m_CohThrAbs) {
            CohThrAbs = fieldInfos.coheVVMaxValue - m_CohThrBase;
        }

        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            retAllMergedValues[i].coherenceBase = (retAllMergedValues[i].cohChange >= m_CohThrBase);
            retAllMergedValues[i].coherenceHigh = (retAllMergedValues[i].cohChange >= m_CohThrHigh);
            retAllMergedValues[i].coherencePresence = (retAllMergedValues[i].cohMax >= CohThrAbs);
            retAllMergedValues[i].candidateCoherence =
                    (retAllMergedValues[i].coherenceBase || retAllMergedValues[i].coherencePresence);
        }
    }

    bool HarvestEvaluation(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                           HarvestInfoType &harvestInfos) {
        // TODO: This can be done using a flag in a structure encapsulating also retAllMergedValues
        //      in order to avoid this iteration (can be done in an iteration previously made)
        harvestInfos.harvestConfirmed = NOT_AVAILABLE;

        if (!HasValidNdviValues(retAllMergedValues, m_OpticalThrVegCycle)) {
            return false;
        }
        harvestInfos.evaluation.fieldId = fieldInfos.fieldId;
        harvestInfos.evaluation.year = fieldInfos.year;
        harvestInfos.evaluation.ttVegStartTime = fieldInfos.ttVegStartTime;
        harvestInfos.evaluation.ttHarvestStartTime = fieldInfos.ttHarvestStartTime;
        harvestInfos.evaluation.ttHarvestEndTime = fieldInfos.ttHarvestEndTime;
        harvestInfos.evaluation.practiceName = fieldInfos.practiceName;
        harvestInfos.evaluation.ttPracticeStartTime = fieldInfos.ttPracticeStartTime;
        harvestInfos.evaluation.ttPracticeEndTime = fieldInfos.ttPracticeEndTime;


        int idxFirstHarvest = -1;
        for(size_t i = 0; i<retAllMergedValues.size(); i++) {
            retAllMergedValues[i].harvest = (retAllMergedValues[i].ndviPresence &&
                    retAllMergedValues[i].candidateOptical &&
                    retAllMergedValues[i].candidateAmplitude &&
                    retAllMergedValues[i].amplitudePresence &&
                    retAllMergedValues[i].candidateCoherence);
            // save the first == week of harvest
            if (retAllMergedValues[i].harvest && idxFirstHarvest == -1) {
                idxFirstHarvest = i;
                harvestInfos.harvestConfirmed = retAllMergedValues[i].ttDate;
            }
        }

        // "HARVESTED CONDITIONS NOT DETECTED"
        if (harvestInfos.harvestConfirmed == NOT_AVAILABLE) {
            // report results from last available week
            int lastAvailableIdx = retAllMergedValues.size() - 1;
            harvestInfos.evaluation.ndviPresence = retAllMergedValues[lastAvailableIdx].ndviPresence;
            harvestInfos.evaluation.candidateOptical = retAllMergedValues[lastAvailableIdx].candidateOptical;
            harvestInfos.evaluation.candidateAmplitude = retAllMergedValues[lastAvailableIdx].candidateAmplitude;
            harvestInfos.evaluation.amplitudePresence = retAllMergedValues[lastAvailableIdx].amplitudePresence;
            harvestInfos.evaluation.candidateCoherence = retAllMergedValues[lastAvailableIdx].candidateCoherence;
            harvestInfos.evaluation.harvestConfirmWeek = NOT_AVAILABLE;
        } else if (idxFirstHarvest != -1) {
            harvestInfos.evaluation.ndviPresence = retAllMergedValues[idxFirstHarvest].ndviPresence;
            harvestInfos.evaluation.candidateOptical = retAllMergedValues[idxFirstHarvest].candidateOptical;
            harvestInfos.evaluation.candidateAmplitude = retAllMergedValues[idxFirstHarvest].candidateAmplitude;
            harvestInfos.evaluation.amplitudePresence = retAllMergedValues[idxFirstHarvest].amplitudePresence;
            harvestInfos.evaluation.candidateCoherence = retAllMergedValues[idxFirstHarvest].candidateCoherence;
            harvestInfos.evaluation.harvestConfirmWeek = GetWeekFromDate(retAllMergedValues[idxFirstHarvest].ttDate);
        }

        return true;
    }

    //  TIME SERIES ANALYSIS FOR EFA PRACTICES
    bool CatchCropPracticeAnalysis(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                                        const HarvestInfoType &harvestInfos, HarvestInfoType &ccHarvestInfos) {
        time_t ttDateA;     // last possible start of catch-crop period
        time_t ttDateB;     // last possible end of catch-crop period
        time_t ttDateC;
        time_t ttDateD = NOT_AVAILABLE;

        // TODO: Ask Gisat about catch.period - is not defined for CZE

        time_t weekA;
        time_t weekB;
        time_t catchStart;

        ccHarvestInfos = harvestInfos;

        if (harvestInfos.evaluation.ttPracticeEndTime == 0) {
            ttDateA = harvestInfos.evaluation.ttPracticeStartTime;
            ttDateB = ttDateA - m_CatchPeriod * SEC_IN_DAY;
            weekA = harvestInfos.evaluation.ttPracticeStartTime;
            weekB = FloorDateToWeekStart(ttDateB);
            if (IsNA(harvestInfos.evaluation.harvestConfirmWeek)) {
                 // harvest is "NR", set the start of catch-crop period to the last possible start of
                // catch-crop period (to select the NDVI values)
                catchStart = GetWeekFromDate(ttDateA);
            } else {
                int lastValidHarvestIdx = -1;
                for (size_t i = 0; i<retAllMergedValues.size(); i++) {
                    // after the last possible start of catch-crop period + 7 days  all the weeks with
                    // the potential $harvest conditions are set to FALSE
                    if (retAllMergedValues[i].ttDate > weekA + SEC_IN_WEEK) {
                        retAllMergedValues[i].harvest = false;
                    } else {
                        // select the weeks with potential $harvest conditions (== bare land)
                        lastValidHarvestIdx = i;
                    }
                }
                // set the start of catch-crop period to the last potential harvest week  or the harvest week from the harvest evaluation
                if (lastValidHarvestIdx != -1) {
                    catchStart = GetWeekFromDate(retAllMergedValues[lastValidHarvestIdx].ttDate);
                } else {
                    catchStart = harvestInfos.evaluation.harvestConfirmWeek;
                }
            }
            // set the start of the catch-crop (first possible date)
            ttDateC = GetTimeOffsetFromStartOfYear(harvestInfos.evaluation.year, catchStart);
            time_t ttCatchPeriodStart = GetTimeFromString(m_CatchPeriodStart);
            if (m_CatchPeriodStart.size() > 0) {
                if (ttDateC < ttCatchPeriodStart) {
                    // a variable can be used to define the earliest date the catch-crop can be sown (???)
                    ttDateC = ttCatchPeriodStart;
                }
            }

            // check if the period is covered with data
            if ((ttDateB - ttDateC) >= m_CatchPeriod * SEC_IN_DAY) {
                // In the original script, here it was a calculation of the EFA markers
                // but function f.efa.markers is used to get the $ndvi.mean for selected weeks <date.c; date.b>

                // TODO: When filtering, the mean values might change for the limits of the interval
                const std::vector<MergedAllValInfosType> &filteredAllMergedValues = FilterValuesByDates(
                            retAllMergedValues, ttDateC, ttDateB);
                // if there is any NDVI value
                // select the maximum NDVI
                double minVal, maxVal, efaMin, efaMax;
                // function f.efa.markers is used to get the $ndvi.mean for selected weeks <date.c; date.b>
                if (GetMinMaxNdviValues(filteredAllMergedValues, minVal, maxVal)) {
                    efaMax = maxVal;    // select the maximum NDVI
                    efaMin = minVal;    // select the minimum NDVI
                    if( efaMin < m_EfaNdviDown ){ efaMin = m_EfaNdviDown; }                 // efa.min is >= efa.ndvi.dw
                    if( efaMin > m_EfaNdviUp ){ efaMin = m_EfaNdviUp; }                 // efa.min is <= efa.ndvi.up if efa.min>ndvi.up !
                } else {
                    // else set it to efa.ndvi.dw
                    efaMax = m_EfaNdviDown;
                    efaMin = m_EfaNdviDown;
                }
                double efaChange = (efaMax - efaMin) * m_CatchProportion;                  // compute ndvi buffer
                if( efaChange <10 ) { efaChange = 10; }
                // Date from which the $candidate.efa (potential bare-land conditions) are relevant to compute
                if (!IsNA(harvestInfos.evaluation.harvestConfirmWeek)) {
                    ttDateD = GetTimeOffsetFromStartOfYear(harvestInfos.evaluation.year, harvestInfos.evaluation.harvestConfirmWeek);
                } else {
                    // get first day of the posible harvest weeks
                    for (size_t i = 0; i<retAllMergedValues.size(); i++) {
                        if (retAllMergedValues[i].candidateOptical) {
                            ttDateD = retAllMergedValues[i].ttDate;
                            break;
                        }
                    }
                    if (ttDateD == NOT_AVAILABLE) {
                        ttDateD = ttDateC;
                    }
                }
                bool curCatchStart;
                int lastCatchStartIdx = -1;
                // Compute $candidate.efa and define the start of the catch-crop period
                for (size_t i = 0; i<retAllMergedValues.size(); i++) {
                    retAllMergedValues[i].candidateEfa = retAllMergedValues[i].ndviNext < (efaMin + efaChange);
                    // potential bare-land conditions (based on NDVI)
                    if (retAllMergedValues[i].ttDate > weekA + 7) {
                        // after the last possible start of catch-crop period + 7 days all the weeks with
                        // the potential bare-land conditions are set to FALSE
                        retAllMergedValues[i].candidateEfa = false;
                    }
                    // before the date.d all the weeks with the potential bare-land conditions are set to FALSE
                    if (retAllMergedValues[i].ttDate < ttDateD) {
                        retAllMergedValues[i].candidateEfa = false;
                    }
                    // define the bare-land conditions from all three time-series == breaks
                    curCatchStart = retAllMergedValues[i].candidateEfa &&
                            retAllMergedValues[i].candidateAmplitude &&
                            retAllMergedValues[i].candidateCoherence;
                    if (curCatchStart) {
                        // weeks with defined breaks
                        lastCatchStartIdx = i;
                    }
                }
                if (lastCatchStartIdx != -1) {
                    // last break == week of the most probalbe start of the catch-crop period
                    catchStart = GetWeekFromDate(retAllMergedValues[lastCatchStartIdx].ttDate);
                } else {
                    // if the period is not covered with data set the start of catch-crop period to the last possible start of catch-crop period
                    catchStart = GetWeekFromDate(ttDateA);
                }
            }
            if (m_CatchPeriodStart != "") {
                // a variable can be used to define the earliest start of the catch-crop period
                int catchPeriodStartWeek = GetWeekFromDate(GetTimeFromString(m_CatchPeriodStart));
                if (catchStart < catchPeriodStartWeek) {
                    catchStart = catchPeriodStartWeek;
                }
            }
            // set the first day of the catch-crop period
            ccHarvestInfos.evaluation.ttPracticeStartTime = GetTimeOffsetFromStartOfYear(harvestInfos.evaluation.year, catchStart);
            // set the last day of the catch-crop period
            ccHarvestInfos.evaluation.ttPracticeEndTime = ccHarvestInfos.evaluation.ttPracticeStartTime + (m_CatchPeriod - 1);
        }

        // ### EFA practice period ###
        ttDateA = ccHarvestInfos.evaluation.ttPracticeStartTime;
        ttDateB = ccHarvestInfos.evaluation.ttPracticeEndTime;
        weekA = FloorDateToWeekStart(ttDateA);
        weekB = FloorDateToWeekStart(ttDateB);

        //bool efaPeriodStarted = retAllMergedValues[retAllMergedValues.size() - 1].ttDate > weekA;
        bool efaPeriodEnded = retAllMergedValues[retAllMergedValues.size() - 1].ttDate >= weekB;

//      DEBUG
//        std::cout << TimeToString(weekA) << std::endl;
//        std::cout << TimeToString(weekB) << std::endl;
//        std::cout << TimeToString(retAllMergedValues[retAllMergedValues.size() - 1].ttDate) << std::endl;
//      DEBUG

        // ### BEFORE end of EFA practice period ###
        if (!efaPeriodEnded) {
            // EFA practice is not evaluated before the end of the EFA practice period - return "NR" evaluation
            ccHarvestInfos.evaluation.efaIndex = "NR";
            ccHarvestInfos.evaluation.ndviPresence = NR;                        // M6
            ccHarvestInfos.evaluation.ndviGrowth = NR;                          // M7
            ccHarvestInfos.evaluation.ndviNoLoss = NR;                          // M8
            ccHarvestInfos.evaluation.ampNoLoss = NR;                           // M9
            ccHarvestInfos.evaluation.cohNoLoss = NR;                           // M10
            return true;
        }

        // ### EFA PRACTICE EVALUATION ###

        // is catch-crop grown in/under the main crop on the parcel
        bool catchInMaincrop = (ccHarvestInfos.evaluation.practiceName == m_CatchMain);
        time_t ttVegSeasonStart = FloorDateToWeekStart(ccHarvestInfos.evaluation.ttVegStartTime);

//      DEBUG
//        std::cout << TimeToString(ttVegSeasonStart) << std::endl;
//      DEBUG

        // is there any evidence of main crop (>opt.thr.vegcycle) in the vegetation season
        bool vegSeasonStatus = false;
        for (size_t i = 0; i<retAllMergedValues.size(); i++) {
            if (!IsNA(retAllMergedValues[i].vegWeeks) && retAllMergedValues[i].vegWeeks == true &&
                    retAllMergedValues[i].ndviMeanVal > m_OpticalThrVegCycle) {
                vegSeasonStatus = true;
                break;
            }
        }
        if (!vegSeasonStatus) {
            // # no evidence of the main crop vegetation in the vegetation season - evaluation of catch-crop is not relevant - return "NR"
            ccHarvestInfos.evaluation.efaIndex = "NR";
            ccHarvestInfos.evaluation.ndviPresence = NR;                        // M6
            ccHarvestInfos.evaluation.ndviGrowth = NR;                          // M7
            ccHarvestInfos.evaluation.ndviNoLoss = NR;                          // M8
            ccHarvestInfos.evaluation.ampNoLoss = NR;                           // M9
            ccHarvestInfos.evaluation.cohNoLoss = NR;                           // M10
            return true;

        } else if (IsNA(ccHarvestInfos.evaluation.harvestConfirmWeek)) {
            if (catchInMaincrop) {
                // harvest was not detected but catch-crop was sown in/under the main crop - evaluate
                ccHarvestInfos.harvestConfirmed = NOT_AVAILABLE;
            } else {
                bool found = false;
                for (size_t i = 0; i<retAllMergedValues.size(); i++) {
                    if (retAllMergedValues[i].ttDate >= ttVegSeasonStart && retAllMergedValues[i].ttDate <= weekA) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    // harvest was not detected but there is "drop" in NDVI before the catch-crop period - evaluate
                    ccHarvestInfos.harvestConfirmed = NOT_AVAILABLE;
                } else {
                    ccHarvestInfos.evaluation.efaIndex = "POOR";
                    return true;
                }
            }
        } else {
            ccHarvestInfos.harvestConfirmed = GetTimeOffsetFromStartOfYear(harvestInfos.evaluation.year, ccHarvestInfos.evaluation.harvestConfirmWeek);
            if (ccHarvestInfos.harvestConfirmed > weekA && !catchInMaincrop) {
                ccHarvestInfos.evaluation.efaIndex = "POOR";
                return true;
            }
        }

        return CCEfaMarkersExtraction(ttDateA, ttDateB, weekA, fieldInfos, ccHarvestInfos);
    }

    bool CCEfaMarkersExtraction(time_t ttDateA, time_t ttDateB, time_t weekA,
                             const FieldInfoType &fieldInfos,
                             HarvestInfoType &ccHarvestInfos) {
        // get efa markers for the defined catch-crop period
        std::vector<EfaMarkersInfoType> efaMarkers;
        ExtractEfaMarkers(ttDateA, ttDateB, fieldInfos, efaMarkers);

        // ### MARKER 6 - efa.presence.ndvi ###
        // # if there is evidence of NDVI>thr in the catch-crop period - M6 is TRUE
        EfaMarkersInfoType efa;
        efa.ndviPresence = IsNdviPresence(efaMarkers);

        // # if there is no NDVI value from third week of EFA practice period and M6 is FALSE,
        //   set the M6 to NA (low values are allowed at the beginning of the period)
        if (!efa.ndviPresence) {
            bool allNdviNA = true;
            for(size_t i = 0; i<efaMarkers.size(); i++) {
                if (efaMarkers[i].ttDate >= weekA + 21 * SEC_IN_DAY) {
                    if (!IsNA(efaMarkers[i].ndviMean)) {
                        allNdviNA = false;
                        break;
                    }
                }
            }
            if (allNdviNA) {
                efa.ndviPresence = NOT_AVAILABLE;
            }
        }

        // ### MARKER 7 - efa.growth.ndvi ###
        // # if all the NDVI are >thr from third week of EFA practice period - M7 is TRUE
        bool allNdviGrowthOK = true;
        bool allNdviGrowthNA = true;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            if (efaMarkers[i].ttDate >= weekA + 21 * SEC_IN_DAY) {
                if (!IsNA(efaMarkers[i].ndviGrowth)) {
                    allNdviGrowthNA = false;
                    if (efaMarkers[i].ndviGrowth == false) {
                        allNdviGrowthOK = false;
                        break;
                    }
                }
            }
        }
        efa.ndviGrowth = allNdviGrowthOK;

        if (IsNA(efa.ndviPresence) || allNdviGrowthNA) {
            efa.ndviGrowth = NOT_AVAILABLE;
        }

        // ### MARKER 8 - efa.noloss.ndvi ###
        if (efa.ndviPresence == NOT_AVAILABLE) {
            efa.ndviNoLoss = NOT_AVAILABLE;
        } else {
            bool allNoLossNA = true;
            bool allNoLossTrue = true;
            for(size_t i = 0; i<efaMarkers.size(); i++) {
                if (!IsNA(efaMarkers[i].ndviNoLoss)) {
                    allNoLossNA = false;
                    if (efaMarkers[i].ndviNoLoss == false) {
                        allNoLossTrue = false;
                        break;
                    }
                }
            }
            if (allNoLossNA) {
                efa.ndviNoLoss = NOT_AVAILABLE;
            } else {
                efa.ndviNoLoss = allNoLossTrue;
            }
        }

        // ### NDVI markers evaluation ###
        // # evaluate the NDVI markers (M6,M7,M8)
        short efaNdvi;
        if (IsNA(efa.ndviPresence) && IsNA(efa.ndviGrowth) && IsNA(efa.ndviNoLoss)) {
            efaNdvi = NOT_AVAILABLE;
        } else {
            // if not all flags are true
            efaNdvi = true;
            if(efa.ndviPresence == false || efa.ndviNoLoss == false || efa.ndviGrowth == false) {
                efaNdvi = false;
            }
        }

        // ### MARKER 9 - backscatter no loss - efa.grd ###
        // # if there is no loss in backscatter - M9 is TRUE
        bool allAmpLossNA = true;
        bool allAmpLossTrue = true;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            if (!IsNA(efaMarkers[i].ampNoLoss)) {
                allAmpLossNA = false;
                if (efaMarkers[i].ampNoLoss == false) {
                    allAmpLossTrue = false;
                    break;
                }
            }
        }
        efa.ampNoLoss = allAmpLossNA ? NOT_AVAILABLE : allAmpLossTrue;

        // ### MARKER 10 - coherence no loss - efa.coh ###
        // # if there is no loss in coherence from third week of the EFA practice period - M10 is TRUE
        bool allCohLossTrue = true;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            if (efaMarkers[i].ttDate >= weekA + 14 * SEC_IN_DAY) {
                if (!IsNA(efaMarkers[i].cohNoLoss)) {
                    if (efaMarkers[i].cohNoLoss == false) {
                        allCohLossTrue = false;
                        break;
                    }
                }
            }
        }
        efa.cohNoLoss = allCohLossTrue;

        // number the FALSE markers
        int efaNotCompliant = 0;
        efaNotCompliant += (efa.ndviPresence == false ? 1 : 0);
        efaNotCompliant += (efa.ndviGrowth == false ? 1 : 0);
        efaNotCompliant += (efa.ndviNoLoss == false ? 1 : 0);
        efaNotCompliant += (efa.ampNoLoss == false ? 1 : 0);
        efaNotCompliant += (efa.cohNoLoss == false ? 1 : 0);

        // # no NDVI values in the catch-crop perid - evaluation is based only on SAR markers (M9,M10)
        if (efaNdvi == NOT_AVAILABLE) {
            if (efaNotCompliant == 0) {
                ccHarvestInfos.evaluation.efaIndex = "MODERATE";
            } else {
                ccHarvestInfos.evaluation.efaIndex = "WEAK";
            }
        } else {
            switch (efaNotCompliant) {
                case 0:
                    ccHarvestInfos.evaluation.efaIndex = "STRONG";
                    break;
                case 1:
                    ccHarvestInfos.evaluation.efaIndex = "MODERATE";
                    break;
                case 2:
                    ccHarvestInfos.evaluation.efaIndex = "WEAK";
                    break;
                default:
                    ccHarvestInfos.evaluation.efaIndex = "POOR";
                    break;
            }
        }
        ccHarvestInfos.evaluation.ndviPresence = efa.ndviPresence;
        ccHarvestInfos.evaluation.ndviGrowth = efa.ndviGrowth;
        ccHarvestInfos.evaluation.ndviNoLoss = efa.ndviNoLoss;
        ccHarvestInfos.evaluation.ampNoLoss = efa.ampNoLoss;
        ccHarvestInfos.evaluation.cohNoLoss = efa.cohNoLoss;

        return true;
    }

    bool FallowLandPracticeAnalysis(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                                        const HarvestInfoType &harvestInfos, HarvestInfoType &flHarvestInfos) {
        flHarvestInfos = harvestInfos;

        time_t ttDateA = fieldInfos.ttPracticeStartTime;     // last possible start of catch-crop period
        time_t ttDateB = fieldInfos.ttPracticeEndTime;     // last possible end of catch-crop period
        time_t weekA = fieldInfos.ttPracticeStartWeekFloorTime;
        time_t weekB = fieldInfos.ttPracticeEndWeekFloorTime;

        time_t ttDateC = GetTimeFromString(m_flMarkersStartDateStr);
        time_t ttDateD = GetTimeFromString(m_flMarkersEndDateStr);
        time_t weekC = FloorDateToWeekStart(ttDateC);
        time_t weekD = FloorDateToWeekStart(ttDateD);

        time_t lastDate = retAllMergedValues[retAllMergedValues.size()-1].ttDate;
        bool efaPeriodEnded = (lastDate >= weekB);

        // ### BEFORE end of EFA practice period ###
        if (!efaPeriodEnded) {
            // # EFA practice is not evaluated before the end of the EFA practice period - return "NR" evaluation
            flHarvestInfos.evaluation.efaIndex = "NR";
            flHarvestInfos.evaluation.ndviPresence = NR;                        // M6
            flHarvestInfos.evaluation.ndviGrowth = NR;                          // M7
            flHarvestInfos.evaluation.ndviNoLoss = NR;                          // M8
            flHarvestInfos.evaluation.ampNoLoss = NR;                           // M9
            flHarvestInfos.evaluation.cohNoLoss = NR;                           // M10
            return true;
        }

        // ### EFA PRACTICE EVALUATION ###
        // get efa markers for the defined catch-crop period
        std::vector<EfaMarkersInfoType> efaMarkers;
        ExtractEfaMarkers(ttDateA, ttDateB, fieldInfos, efaMarkers);

        // ### MARKER 6 - efa.presence.ndvi ###
        // # if there is evidence of vegetation (NDVI>efa.ndvi.thr) in the EFA practice period - M6 is TRUE
        flHarvestInfos.evaluation.ndviPresence = IsNdviPresence(efaMarkers);
        if (!flHarvestInfos.evaluation.ndviPresence) {
            // # no evidence of GREEN FALLOW vegetation in the EFA practice period - return POOR evaluation
            flHarvestInfos.evaluation.efaIndex = "POOR";
            flHarvestInfos.evaluation.ndviGrowth = NR;                          // M7
            flHarvestInfos.evaluation.ndviNoLoss = NR;                          // M8
            flHarvestInfos.evaluation.ampNoLoss = NR;                           // M9
            flHarvestInfos.evaluation.cohNoLoss = NR;                           // M10
            return true;

        } else if (IsNA(harvestInfos.evaluation.harvestConfirmWeek)) {
            // # vegetation is present, harvest was not detected - FALLOW is considered ok - evaluate
            // TODO: see how harvestConfirmWeek is initialized - NOT_AVAILABLE or 0 ?
            flHarvestInfos.harvestConfirmed = NOT_AVAILABLE;
        } else {
            // # set the first day of the harvest week - evaluate
            time_t harvestConfirmed = GetTimeOffsetFromStartOfYear(harvestInfos.evaluation.year, harvestInfos.evaluation.harvestConfirmWeek);
            if (harvestConfirmed > weekA && harvestConfirmed <= weekB) {
                // # harvest was detected within the fallow period - FALLOW is considered not ok - return WEAK evaluation
                flHarvestInfos.evaluation.efaIndex = "WEAK";
                flHarvestInfos.evaluation.ndviGrowth = NR;                          // M7
                flHarvestInfos.evaluation.ndviNoLoss = NR;                          // M8
                flHarvestInfos.evaluation.ampNoLoss = NR;                           // M9
                flHarvestInfos.evaluation.cohNoLoss = NR;                           // M10
                return true;
            }
        }

        // ### MARKER 8 - efa.noloss.ndvi ###
        // # mowing or mulching is required between "2017-06-01" and "2017-08-31" - efa.noloss.ndvi shall be FALSE in this period
        std::vector<EfaMarkersInfoType> efaMarkers2;
        // # get efa markers for the specified period & buffer
        ExtractEfaMarkers(ttDateC - 30 * SEC_IN_DAY, ttDateD + 30 * SEC_IN_DAY, fieldInfos, efaMarkers2);
        for (int i = (int)efaMarkers2.size() - 2; i>=0; i--) {
            if (IsNA(efaMarkers2[i].ndviNoLoss) && !IsNA(efaMarkers2[i+1].ndviNoLoss)) {
                efaMarkers2[i].ndviNoLoss = efaMarkers2[i+1].ndviNoLoss;
            }
        }
        // # if there is evidence of loss in NDVI in the period - M8(noloss) is FALSE
        bool ndviNoLossFalseFound = false;
        for (size_t i = 0; i<efaMarkers2.size(); i++) {
            if (IsNA(efaMarkers2[i].ndviNoLoss)) {
                continue;
            }
            if (efaMarkers2[i].ttDate >= weekC && efaMarkers2[i].ttDate <= weekD + 7 * SEC_IN_DAY) {
                if (efaMarkers2[i].ndviNoLoss == false) {
                    ndviNoLossFalseFound = true;
                    break;
                }
            }
        }
        bool efaNoLossNdvi = !ndviNoLossFalseFound;
        if (!efaNoLossNdvi) {
            flHarvestInfos.evaluation.efaIndex = "STRONG";
        } else {
            flHarvestInfos.evaluation.efaIndex = "MODERATE";
        }

        flHarvestInfos.evaluation.ndviGrowth = NR;                          // M7
        flHarvestInfos.evaluation.ampNoLoss = NR;                           // M9
        flHarvestInfos.evaluation.cohNoLoss = NR;                           // M10

        return true;
    }

    bool NitrogenFixingCropPracticeAnalysis(const FieldInfoType &fieldInfos, std::vector<MergedAllValInfosType> &retAllMergedValues,
                                        const HarvestInfoType &harvestInfos, HarvestInfoType &ncHarvestInfos) {
        ncHarvestInfos = harvestInfos;

        time_t ttDateA = fieldInfos.ttPracticeStartTime;     // last possible start of catch-crop period
        time_t ttDateB = fieldInfos.ttPracticeEndTime;     // last possible end of catch-crop period
        time_t weekA = fieldInfos.ttPracticeStartWeekFloorTime;
        time_t weekB = fieldInfos.ttPracticeEndWeekFloorTime;

        time_t lastDate = retAllMergedValues[retAllMergedValues.size()-1].ttDate;
        bool efaPeriodEnded = (lastDate >= weekB);

        // ### BEFORE end of EFA practice period ###
        if (!efaPeriodEnded) {
            // # EFA practice is not evaluated before the end of the EFA practice period - return "NR" evaluation
            ncHarvestInfos.evaluation.efaIndex = "NR";
//            efa.index <- "NR"                                                       # EFA practice is not evaluated before the end of the EFA practice period - return "NR" evaluation
//            efa.evaluation <- data.frame( "NR","NR","NR","NR","NR", efa.index )
//            colnames(efa.evaluation) <- c("M6","M7","M8","M9","M10", "C_INDEX")
//            harvest.evaluation <- data.frame(harvest.evaluation,efa.evaluation)
//            return(harvest.evaluation)
            // TODO: Handle this
            return false;
        }

        // ### EFA PRACTICE EVALUATION ###
        // get efa markers for the defined catch-crop period
        time_t vegSeasonStart = FloorDateToWeekStart(harvestInfos.evaluation.ttVegStartTime);
        std::vector<EfaMarkersInfoType> efaMarkers;
        ExtractEfaMarkers(vegSeasonStart, ttDateB, fieldInfos, efaMarkers);

        // ### MARKER 6 - efa.presence.ndvi ###
        EfaMarkersInfoType efa;
        // # if there is evidence of vegetation (NDVI>efa.ndvi.thr) in the vegetation season - M6 is TRUE
        efa.ndviPresence = IsNdviPresence(efaMarkers);
        if (!efa.ndviPresence) {
            // # no evidence of the NFC vegetation in the vegetation season - return POOR evaluation
            ncHarvestInfos.evaluation.efaIndex = "POOR";

            // TODO:

//            efa.evaluation <- data.frame( efa.presence.ndvi,"NR","NR","NR","NR", efa.index )
//            colnames(efa.evaluation) <- c("M6","M7","M8","M9","M10", "C_INDEX")
//            harvest.evaluation <- data.frame(harvest.evaluation,efa.evaluation)
//            return(harvest.evaluation)

            return false;
        } else if (IsNA(harvestInfos.evaluation.harvestConfirmWeek)) {
            // # vegetation is present, harvest was not detected - NFC is considered ok - evaluate
            // TODO: see how harvestConfirmWeek is initialized - NOT_AVAILABLE or 0 ?
            ncHarvestInfos.harvestConfirmed = NOT_AVAILABLE;
        } else {
            // # set the first day of the harvest week - evaluate
            time_t harvestConfirmed = GetTimeOffsetFromStartOfYear(harvestInfos.evaluation.year, harvestInfos.evaluation.harvestConfirmWeek);
            if (harvestConfirmed > weekA && harvestConfirmed <= weekB) {
                // # harvest detected within the efa practice period - NFC is considered not ok - return WEAK evaluation
                ncHarvestInfos.evaluation.efaIndex = "WEAK";
                // TODO
//                efa.evaluation <- data.frame( efa.presence.ndvi,"NR","NR","NR","NR", efa.index )
//                colnames(efa.evaluation) <- c("M6","M7","M8","M9","M10", "C_INDEX")
//                harvest.evaluation <- data.frame(harvest.evaluation,efa.evaluation)
                return false;
            }
        }

        // ### MARKER 6 - efa.presence.ndvi ###
        std::vector<EfaMarkersInfoType> efaMarkers2;
        // # get efa markers for the specified period & buffer
        ExtractEfaMarkers(ttDateA, ttDateB, fieldInfos, efaMarkers2);

        // #  if there is evidence of vegetation (NDVI>efa.ndvi.thr) in the EFA practice period - M6 is TRUE
        bool ndviPresenceTrueFound = false;
        for (size_t i = 0; i<efaMarkers2.size(); i++) {
            if (IsNA(efaMarkers2[i].ndviPresence)) {
                continue;
            }
            if (efaMarkers2[i].ndviPresence == true) {
                ndviPresenceTrueFound = true;
                break;
            }
        }
        bool efaPresenceNdvi = ndviPresenceTrueFound;
        if (efaPresenceNdvi) {
            ncHarvestInfos.evaluation.efaIndex = "STRONG";
        } else {
            ncHarvestInfos.evaluation.efaIndex = "MODERATE";
        }

        // TODO:
//        efa.evaluation <- data.frame( efa.presence.ndvi,"NR","NR","NR","NR", efa.index )
//        colnames(efa.evaluation) <- c("M6","M7","M8","M9","M10", "C_INDEX")
//        harvest.evaluation <- data.frame(harvest.evaluation,efa.evaluation)

        return true;
    }

    bool ExtractEfaMarkers(time_t ttStartTime, time_t ttEndTime, const FieldInfoType &fieldInfos,
                           std::vector<EfaMarkersInfoType> &efaMarkers) {

        const std::vector<InputFileLineInfoType> &ndviFilteredValues = FilterValuesByDates(
                    fieldInfos.ndviLines, ttStartTime, ttEndTime);
        const std::vector<InputFileLineInfoType> &coheVVFilteredValues = FilterValuesByDates(
                    fieldInfos.coheVVLines, ttStartTime, ttEndTime);
//        const std::vector<InputFileLineInfoType> &coheVHFilteredValues = FilterValuesByDates(
//                    fieldInfos.coheVHLines, ttStartTime, ttEndTime);

        // TODO: Ask Gisat why amplitude is taken for an interval > 1 week?
        const std::vector<InputFileLineInfoType> &ampVVFilteredValues = FilterValuesByDates(
                    fieldInfos.ampVVLines, ttStartTime, ttEndTime + SEC_IN_WEEK);
        const std::vector<InputFileLineInfoType> &ampVHFilteredValues = FilterValuesByDates(
                    fieldInfos.ampVHLines, ttStartTime, ttEndTime + SEC_IN_WEEK);

        // TODO: the code below is the same with another one before
        std::vector<MergedDateAmplitudeType> mergedAmpInfos;
        if (!MergeAmplitudes(fieldInfos.fieldId, ampVVFilteredValues, ampVHFilteredValues, mergedAmpInfos)) {
            return false;
        }
        otbAppLogINFO("Grouping amplitudes by weeks for field  " << fieldInfos.fieldId);
        // Group backscatter ratio by weeks (7-day period) - compute week-mean ($grd.mean)
        std::vector<GroupedMeanValInfosType> ampRatioGroups;
        if (!GroupTimedValuesByWeeks(mergedAmpInfos, ampRatioGroups)) {
            return false;
        }

        otbAppLogINFO("Computing amplitude 3 weeks mean - current value mean for field  " << fieldInfos.fieldId);
        // Compute 3-weeks-mean before a week ($grd.w3m)
        // and compute difference between week-mean and previous 3-week-mean ($grd.change)
        if (!Compute3WeeksAmplRatioDiffs(ampRatioGroups)) {
            return false;
        }

        otbAppLogINFO("Grouping NDVI by weeks for field  " << fieldInfos.fieldId);
        std::vector<GroupedMeanValInfosType> ndviGroups;
        if (!GroupTimedValuesByWeeks(ndviFilteredValues, ndviGroups)) {
            return false;
        }

        // round the values from the NDVI groupes
        std::transform(ndviGroups.begin(), ndviGroups.end(), ndviGroups.begin(),
                       [](GroupedMeanValInfosType &x) {
                x.meanVal = std::round(x.meanVal);
                return x;
        });

        std::vector<GroupedMeanValInfosType> coherenceGroups;
        otbAppLogINFO("Grouping Coherence by weeks for field  " << fieldInfos.fieldId);
        if (!GroupTimedValuesByWeeks(coheVVFilteredValues, coherenceGroups)) {
            return false;
        }

        // round the coherence change values
        // round the values from the NDVI groupes
        std::transform(coherenceGroups.begin(), coherenceGroups.end(), coherenceGroups.begin(),
                       [](GroupedMeanValInfosType &x) {
                x.maxChangeVal = (std::round(x.maxChangeVal * 1000) / 1000);
                return x;
        });
        otbAppLogINFO("Merging all information for field  " << fieldInfos.fieldId);
        std::vector<MergedAllValInfosType> allMergedValues;
        if (!MergeAllFieldInfos(fieldInfos, ampRatioGroups, coherenceGroups,
                                ndviGroups, allMergedValues)) {
            return false;
        }

        efaMarkers.resize(allMergedValues.size());

//      DEBUG
        std::cout << TimeToString(ttStartTime) << std::endl;
        std::cout << TimeToString(ttEndTime) << std::endl;
        PrintEfaMarkers(allMergedValues, efaMarkers);
//      DEBUG

        // ### COMPUTE EFA MARKERS ###

        // ### MARKER 6 - $ndvi.presence ###

        int prevNotNaNdviIdx = -1;
        bool allNdviLessDw = true;
        bool ndviLessDwFound = false;
        bool ndviLessUpFound = false;
        // TODO: Check if duplicated code with UpdateMarker2Infos
        double minMeanNdviDropVal = NOT_AVAILABLE;

        double efaNdviUp = m_EfaNdviUp;
        double efaNdviDown = m_EfaNdviDown;
        double efaNdviMin = m_EfaNdviMin;

        // TODO: we can set 300 as default value and not setting it here
        if (IsNA(efaNdviMin)) {
            efaNdviMin = 300;
        }

        // TODO: in the code below we might have access violations due to ndviCoheFilteredValues and ampFilteredValues different sizes
        for (size_t i = 0; i<allMergedValues.size(); i++) {
            efaMarkers[i].ttDate = allMergedValues[i].ttDate;
            efaMarkers[i].ndviPresence = NOT_AVAILABLE;
            efaMarkers[i].ndviDrop = NOT_AVAILABLE;
            efaMarkers[i].ndviGrowth = NOT_AVAILABLE;
            efaMarkers[i].ndviNoLoss = NOT_AVAILABLE;
            efaMarkers[i].ndviMean = allMergedValues[i].ndviMeanVal;
            if (efaMarkers[i].ndviMean == NOT_AVAILABLE) {
                continue;
            }
            efaMarkers[i].ndviPresence = allMergedValues[i].ndviMeanVal >= m_EfaNdviThr;
            if (!IsNA(efaNdviDown) && !IsNA(efaNdviUp)) {
                if (!IsNA(allMergedValues[i].ndviMeanVal)) {
                    // skip the first line
                    if (prevNotNaNdviIdx >= 0) {
                        efaMarkers[i].ndviDrop = ((allMergedValues[i].ndviMeanVal < allMergedValues[prevNotNaNdviIdx].ndviMeanVal) &&
                                                  (allMergedValues[i].ndviMeanVal < efaNdviUp));
                    }
                    prevNotNaNdviIdx = i;
                    // not all ndvi are less than ndvi down
                    if (allMergedValues[i].ndviMeanVal >= efaNdviDown) {
                        allNdviLessDw = false;
                    }
                    if (efaMarkers[i].ndviDrop == true) {
                        if (allMergedValues[i].ndviMeanVal < efaNdviDown) {
                            // at least one ndvi is less than ndvi down
                            ndviLessDwFound = true;
                        }
                        if (allMergedValues[i].ndviMeanVal < efaNdviUp) {
                            ndviLessUpFound = true;
                        }

                        if (IsNA(minMeanNdviDropVal)) {
                            minMeanNdviDropVal = allMergedValues[i].ndviMeanVal;
                        } else {
                            if (minMeanNdviDropVal > allMergedValues[i].ndviMeanVal) {
                                minMeanNdviDropVal = allMergedValues[i].ndviMeanVal;
                            }
                        }
                    }
                }
            }
            // # MARKER 7 - $ndvi.growth ###
            // if both ndviPresence and ndviDrop are NA, we let ndviGrowth to NA
            if (!IsNA(efaMarkers[i].ndviPresence) || !IsNA(efaMarkers[i].ndviDrop)) {
                efaMarkers[i].ndviGrowth = efaMarkers[i].ndviPresence || efaMarkers[i].ndviDrop == false;
            }
            if (IsNA(efaMarkers[i].ndviDrop) && (efaMarkers[i].ndviPresence == false)) {
                efaMarkers[i].ndviGrowth = false;
            }
            if (efaMarkers[i].ndviGrowth == true) {
                efaMarkers[i].ndviGrowth = (allMergedValues[i].ndviMeanVal >= efaNdviMin);
            }

            double opticalThresholdValue;
            if (allNdviLessDw || ndviLessDwFound) {
                opticalThresholdValue = efaNdviDown;
            } else if (ndviLessUpFound) {
                opticalThresholdValue = m_ndviStep * std::ceil(minMeanNdviDropVal / m_ndviStep);
            } else {
                opticalThresholdValue = efaNdviUp;
            }

            // ### MARKER 8 - $ndvi.noloss ###
            efaMarkers[i].ndviNoLoss = !(efaMarkers[i].ndviDrop &&
                                         (allMergedValues[i].ndviMeanVal <= opticalThresholdValue));
            if (IsNA(efaMarkers[i].ndviDrop)) {
                efaMarkers[i].ndviNoLoss = NOT_AVAILABLE;
            }
        }

        // ### MARKER 9 - $grd.noloss ###
//      DEBUG
//        PrintEfaMarkers(allMergedValues, efaMarkers);
//      DEBUG

        const std::vector<MergedDateAmplitude> &ampFilteredRawValues = FilterValuesByDates(
                    mergedAmpInfos, ttStartTime, ttEndTime);

        // extract the weeks array
        std::vector<int> weeks;
        weeks.reserve(ampFilteredRawValues.size());
        std::transform(ampFilteredRawValues.begin(), ampFilteredRawValues.end(),
                       std::back_inserter(weeks), [](MergedDateAmplitude f){return f.vvInfo.weekNo;});

        // sort the values
        std::sort(weeks.begin(), weeks.end());
        // keep the unique weeks values
        weeks.erase(std::unique(weeks.begin(), weeks.end()), weeks.end());


        int minWeek = NOT_AVAILABLE;
        int maxWeek = NOT_AVAILABLE;
        if (weeks.size() > 0) {
            minWeek = weeks[0] + 1;
            maxWeek = weeks[weeks.size() - 1] - 1;
        }
        // it might be due to the fact is the first week of the year
        if (maxWeek == -1 || IsNA(maxWeek)) {
            maxWeek = GetWeekFromDate(allMergedValues[allMergedValues.size()-1].ttDate) - 1;
        }

        std::vector<int> allWeeks;
        allWeeks.reserve(mergedAmpInfos.size());
        std::transform(mergedAmpInfos.begin(), mergedAmpInfos.end(),
                       std::back_inserter(allWeeks), [](MergedDateAmplitude f){return f.vvInfo.weekNo;});
        // sort the values
        std::vector<int> allWeeksUniques = allWeeks;
        std::sort(allWeeksUniques.begin(), allWeeksUniques.end());
        // keep the unique weeks values
        allWeeksUniques.erase(std::unique(allWeeksUniques.begin(), allWeeksUniques.end()), allWeeksUniques.end());

        std::vector<double> ampSlopeVec, ampPValueVec;
        ampSlopeVec.resize(efaMarkers.size());
        std::fill (ampSlopeVec.begin(),ampSlopeVec.end(), NOT_AVAILABLE);
        ampPValueVec.resize(efaMarkers.size());
        std::fill (ampSlopeVec.begin(),ampSlopeVec.end(), NOT_AVAILABLE);

        if (allWeeksUniques.size() >= 3) {
            // linear fitting for +/- 2 weeks
            int curWeek;
            for (int i = minWeek; i<=maxWeek; i++) {
                curWeek = i;
                // extract from the amplitude vectors , the weeks
                std::vector<double> subsetAmpTimes;
                std::vector<double> subsetAmpValues;
                for(size_t j = 0; j<mergedAmpInfos.size(); j++) {
                    if ((mergedAmpInfos[j].vvInfo.weekNo >= curWeek - 2) &&
                        (mergedAmpInfos[j].vvInfo.weekNo <= curWeek + 2)) {
                        subsetAmpTimes.push_back(mergedAmpInfos[j].ttDate / SEC_IN_DAY);
                        subsetAmpValues.push_back(mergedAmpInfos[j].ampRatio);
                    }
                }
                if (subsetAmpTimes.size() < 3) {
                    continue;
                }

                // TODO: Compute also the p-value
                double slope;
                if (ComputeSlope(subsetAmpTimes, subsetAmpValues, slope)) {
                    // compute the marker to be updated
                    int idx = curWeek - minWeek + 1;
                    if (idx >= 0 && idx < (int)ampSlopeVec.size()) {
                        ampSlopeVec[idx] = slope;
                    }
                }

            }
        }

        double slopeThreshold = (m_EfaAmpThr != NOT_AVAILABLE) ? m_EfaAmpThr : 0.01;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            // TODO: Uncomment this after implementation of p-Value
            if (IsNA(ampSlopeVec[i]) /*|| IsNA(ampPValueVec[i])*/) {
                efaMarkers[i].ampNoLoss = NOT_AVAILABLE;
            } else {
                efaMarkers[i].ampNoLoss = !(ampSlopeVec[i] >= slopeThreshold &&
                                            ampPValueVec[i] < 0.05);
            }
        }

        // ### MARKER 10 - $coh.noloss ###
        double cohThrHigh = (m_EfaCohChange != NOT_AVAILABLE) ? m_EfaCohChange : m_CohThrHigh;
        double cohThrAbs = (m_EfaCohValue != NOT_AVAILABLE) ? m_EfaCohValue : m_CohThrAbs;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            bool cohHigh = allMergedValues[i].cohChange >= cohThrHigh;
            bool cohPresence = allMergedValues[i].cohMax >= cohThrAbs;
            efaMarkers[i].cohNoLoss = !(cohHigh || cohPresence);
        }

//      DEBUG
        PrintEfaMarkers(allMergedValues, efaMarkers);
//      DEBUG

        return true;
    }

    bool IsNdviPresence(const std::vector<EfaMarkersInfoType> &efaMarkers) {
        bool ndviPresence = false;
        for(size_t i = 0; i<efaMarkers.size(); i++) {
            if (!IsNA(efaMarkers[i].ndviPresence) && efaMarkers[i].ndviPresence == true) {
                ndviPresence = true;
                break;
            }
        }
        return ndviPresence;
    }

    bool GetMinMaxNdviValues(const std::vector<MergedAllValInfosType> &values, double &minVal, double &maxVal) {
        bool hasValidNdvi = false;
        double curMinVal = NOT_AVAILABLE;
        double curMaxVal = NOT_AVAILABLE;
        for(size_t i = 0; i<values.size(); i++) {
            if (values[i].ndviMeanVal == NOT_AVAILABLE) {
                continue;
            }
            hasValidNdvi = true;
            if (curMinVal == NOT_AVAILABLE) {
                curMinVal = values[i].ndviMeanVal;
                curMaxVal = curMinVal;
            } else {
                if (curMinVal > values[i].ndviMeanVal) {
                    curMinVal = values[i].ndviMeanVal;
                }
                if (curMaxVal < values[i].ndviMeanVal) {
                    curMaxVal = values[i].ndviMeanVal;
                }
            }
        }
        minVal = curMinVal;
        maxVal = curMaxVal;

        return hasValidNdvi;
    }

    bool HasValidNdviValues(const std::vector<MergedAllValInfosType> &values, double thrVal = NOT_AVAILABLE) {
        bool hasValidNdvi = false;
        for(size_t i = 0; i<values.size(); i++) {
            if (values[i].ndviMeanVal == NOT_AVAILABLE) {
                continue;
            }
            if (values[i].ndviMeanVal > thrVal) {
                hasValidNdvi = true;
                break;
            }
        }
        return hasValidNdvi;
    }

    void DisplayFeature(const FeatureDescription& feature)
    {
        std::cout << feature.GetFieldId() << " ; " <<
                     feature.GetCountryCode() << " ; " <<
                     feature.GetYear() << " ; " <<
                     feature.GetMainCrop() << " ; " <<
                     feature.GetVegetationStart() << " ; " <<
                     feature.GetHarvestStart() << " ; " <<
                     feature.GetHarvestEnd() << " ; " <<
                     feature.GetPractice() << " ; " <<
                     feature.GetPracticeType() << " ; " <<
                     feature.GetPracticeStart() << " ; " <<
                     feature.GetPracticeEnd() << std::endl;
    }

    bool ComputeMeanAndStandardDeviation (const std::vector<double> &inVect,
                                          double &meanVal, double &stdDevVal,
                                          bool genSampleStandardDev = false) {
        if (inVect.size() == 0) {
            return false;
        }
        if (genSampleStandardDev && inVect.size() == 1) {
            return false;
        }
        double sum = std::accumulate(inVect.begin(), inVect.end(), 0.0);
        meanVal = sum / inVect.size();

        // Boost implementation
//        boost::accumulators::accumulator_set<double, boost::accumulators::stats<boost::accumulators::tag::variance> > acc;
//        for (int i = 0; i<inVect.size(); i++) {
//            acc(inVect[i]);
//        }
//        //cout << mean(acc) << endl;
//        stdDevVal = sqrt(boost::accumulators::variance(acc));

        // Non boost implementation

        std::vector<double> diff(inVect.size());
        std::transform(inVect.begin(), inVect.end(), diff.begin(), [meanVal](double x) { return x - meanVal; });
        double sq_sum = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
        int vecSize = inVect.size();
        if (genSampleStandardDev) {
            vecSize -= 1;
        }
        stdDevVal = std::sqrt(sq_sum / vecSize);

        return true;
    }

    std::string GetResultsCsvFilePath(const std::string &practiceName, const std::string &countryCode,
                                      int year) {
        const std::string &fileName = "Sen4CAP_L4C_" + practiceName + "_" +
                countryCode + "_" + std::to_string(year) + "_CSV.csv";
        boost::filesystem::path rootFolder(m_outputDir);
        return (rootFolder / fileName).string();
    }

    std::string GetPlotsFilePath(const std::string &practiceName, const std::string &countryCode,
                                      int year) {
        const std::string &fileName = "Sen4CAP_L4C_" + practiceName + "_" +
                countryCode + "_" + std::to_string(year) + "_PLOT.xml";
        boost::filesystem::path rootFolder(m_outputDir);
        return (rootFolder / fileName).string();
    }

    void WriteCSVHeader(const std::string &practiceName, const std::string &countryCode,
                        int year) {
        if (m_OutFileStream.is_open()) {
            return;
        }
        const std::string &fullPath = GetResultsCsvFilePath(practiceName, countryCode, year);
        m_OutFileStream.open(fullPath, std::ios_base::trunc | std::ios_base::out);

        // # create result csv file for harvest and EFA practice evaluation
        m_OutFileStream << "FIELD_ID;COUNTRY;YEAR;MAIN_CROP;VEG_START;H_START;H_END;"
                   "PRACTICE;P_TYPE;P_START;P_END;M1;M2;M3;M4;M5;H_WEEK;M6;M7;M8;M9;M10;C_INDEX\n";
    }

    void WriteHarvestInfoToCsv(const FieldInfoType &fieldInfo, const HarvestInfoType &harvestInfo, const HarvestInfoType &efaHarvestInfo) {
        //"FIELD_ID;COUNTRY;YEAR;MAIN_CROP
        m_OutFileStream << fieldInfo.fieldId << ";" << fieldInfo.countryCode << ";" << ValueToString(fieldInfo.year) << ";" << fieldInfo.mainCrop << ";" <<
                   // VEG_START;H_START;H_END;"
                   TimeToString(fieldInfo.ttVegStartTime) << ";" << TimeToString(fieldInfo.ttHarvestStartTime) << ";" << TimeToString(fieldInfo.ttHarvestEndTime) << ";" <<
                   // "PRACTICE;P_TYPE;P_START;P_END;
                   fieldInfo.practiceName << ";" << fieldInfo.practiceType << ";" << TimeToString(fieldInfo.ttPracticeStartTime) << ";" <<
                   TimeToString(fieldInfo.ttPracticeEndTime) << ";" <<
                   // M1;M2;
                   ValueToString(harvestInfo.evaluation.ndviPresence, true) << ";" << ValueToString(harvestInfo.evaluation.candidateOptical, true) << ";" <<
                   // M3;M4;
                   ValueToString(harvestInfo.evaluation.candidateAmplitude, true) << ";" << ValueToString(harvestInfo.evaluation.amplitudePresence, true) << ";" <<
                   // M5;H_WEEK;
                   ValueToString(harvestInfo.evaluation.candidateCoherence, true) << ";" << ValueToString(harvestInfo.evaluation.harvestConfirmWeek) << ";" <<
                   // M6;M7;
                   ValueToString(efaHarvestInfo.evaluation.ndviPresence, true) << ";" << ValueToString(efaHarvestInfo.evaluation.ndviGrowth, true) << ";" <<
                   // M8;M9;
                   ValueToString(efaHarvestInfo.evaluation.ndviNoLoss, true) << ";" << ValueToString(efaHarvestInfo.evaluation.ampNoLoss, true) << ";" <<
                    //M10;C_INDEX
                   ValueToString(efaHarvestInfo.evaluation.cohNoLoss, true) << ";" << efaHarvestInfo.evaluation.efaIndex << "\n";
    }

    void CreatePlotsFile(const std::string &practiceName, const std::string &countryCode, int year) {
        const std::string &fullPath = GetPlotsFilePath(practiceName, countryCode, year);
        m_OutPlotsFileStream.open(fullPath, std::ios_base::trunc | std::ios_base::out);
        m_OutPlotsFileStream << "<plots>\n";
    }

    void ClosePlotsFile() {
        m_OutPlotsFileStream << "</plots>\n";
    }

    void WritePlotEntry(const FieldInfoType &fieldInfos, const HarvestInfoType &harvestInfo) {
        m_OutPlotsFileStream << " <fid id=\"" << fieldInfos.fieldId.c_str() << "\">\n";
        m_OutPlotsFileStream << " <harvest ps=\"" << TimeToString(harvestInfo.evaluation.ttHarvestStartTime).c_str() <<
                                "\" pe=\"" << TimeToString(harvestInfo.evaluation.ttHarvestEndTime).c_str() <<
                                "\" w=\"" << ValueToString(harvestInfo.evaluation.harvestConfirmWeek).c_str() <<
                                "\"/>\n";
        m_OutPlotsFileStream << "  <ndvis>\n";
        for (size_t i = 0; i<fieldInfos.ndviLines.size(); i++) {
            m_OutPlotsFileStream << "   <ndvi date=\"" << fieldInfos.ndviLines[i].strDate <<
                                    "\" val=\"" << ValueToString(fieldInfos.ndviLines[i].meanVal).c_str() << "\"/>\n";
        }
        m_OutPlotsFileStream << "  </ndvis>\n";
        m_OutPlotsFileStream << "  <amps>\n";
        for (size_t i = 0; i<fieldInfos.ampVVLines.size(); i++) {
            m_OutPlotsFileStream << "   <amp date=\"" << fieldInfos.ampVVLines[i].strDate <<
                                    "\" val=\"" << ValueToString(fieldInfos.ampVVLines[i].meanVal) << "\"/>\n";
        }
        m_OutPlotsFileStream << "  </amps>\n";
        m_OutPlotsFileStream << "  <cohs>\n";
        for (size_t i = 0; i<fieldInfos.coheVVLines.size(); i++) {
            m_OutPlotsFileStream << "   <coh date=\"" << fieldInfos.coheVVLines[i].strDate <<
                                    "\" val=\"" << ValueToString(fieldInfos.coheVVLines[i].meanVal) << "\"/>\n";
        }
        m_OutPlotsFileStream << "  </cohs>\n";
    }

    double ComputeOpticalThresholdBuffer(int nVegWeeksCnt, double maxMeanNdviDropVal, double OpticalThresholdValue) {
        double OpticalThresholdBuffer = -1;
        if (nVegWeeksCnt > 1) {
            OpticalThresholdBuffer = std::round((maxMeanNdviDropVal - OpticalThresholdValue) / m_OpticalThrBufDenominator);
        }
        if (OpticalThresholdBuffer < 0) {
            OpticalThresholdBuffer = 0;
        }
        return OpticalThresholdBuffer;
    }

    double ComputeAmplitudeThresholdValue(double meanVal, double stdDevVal) {
        //        grd.thr.value <- paste("grd.thr.value <- mean(grd.filtered)-sd(grd.filtered)/2")
        // or
        //        grd.thr.value <- paste("grd.thr.value <- mean(grd.filtered)
        return meanVal - (m_UseStdDevInAmpThrValComp ? (stdDevVal / 2) : 0);
    }

    double ComputeAmplitudeThresholdBreak(double stdDevVal) {
        double ampThrBreak = stdDevVal / 6;
        // minimum change
        if (ampThrBreak < m_AmpThrMinimum) {
            ampThrBreak = m_AmpThrMinimum;
        }
        return ampThrBreak;
    }


    void PrintMergedValues(const std::vector<MergedAllValInfosType> &mergedVals) {
        std::cout << "Printing merged values" << std::endl;
        std::cout << "   Date  coh.max   coh.change  grd.mean  grd.max grd.change " << std::endl;

        for(size_t i = 0; i < mergedVals.size(); i++) {
            const MergedAllValInfosType &val = mergedVals[i];
            std::cout << i + 1 << " " <<
                         TimeToString(val.ttDate) << " " <<
                         ValueToString(val.cohMax) << " " <<
                         ValueToString(val.cohChange) << " " <<
                         ValueToString(val.ampMean) << " " <<
                         ValueToString(val.ampMax) << " " <<
                         ValueToString(val.ampChange) << std::endl;
        }
        std::cout << "   ndvi.mean ndvi.prev   ndvi.next  veg.weeks ndvi.presence  ndvi.drop" << std::endl;
        for(size_t i = 0; i < mergedVals.size(); i++) {
            const MergedAllValInfosType &val = mergedVals[i];
            std::cout << i + 1 << " " <<
                         ValueToString(val.ndviMeanVal) << " " <<
                         ValueToString(val.ndviPrev) << " " <<
                         ValueToString(val.ndviNext) << " " <<
                         ValueToString(val.vegWeeks, true) << " " <<
                         ValueToString(val.ndviPresence, true) << " " <<
                         ValueToString(val.ndviDrop, true) << " " <<
                         std::endl;
        }
        std::cout << "   candidate.opt coh.base   coh.high  coh.presence candidate.coh  candidate.grd" << std::endl;
        for(size_t i = 0; i < mergedVals.size(); i++) {
            const MergedAllValInfosType &val = mergedVals[i];
            std::cout << i + 1 << " " <<
                         ValueToString(val.candidateOptical, true) << " " <<
                         ValueToString(val.coherenceBase, true) << " " <<
                         ValueToString(val.coherenceHigh, true) << " " <<
                         ValueToString(val.coherencePresence, true) << " " <<
                         ValueToString(val.candidateCoherence, true) << " " <<
                         ValueToString(val.candidateAmplitude, true) << " " <<
                         std::endl;
        }
        std::cout << "   grd.presence " << std::endl;
        for(size_t i = 0; i < mergedVals.size(); i++) {
            const MergedAllValInfosType &val = mergedVals[i];
            std::cout << i + 1 << " " <<
                         ValueToString(val.amplitudePresence, true) << " " <<
                         std::endl;
        }

    }

    void PrintAmplitudeInfos(const FieldInfoType &fieldInfos) {
        std::cout << "Printing amplitude values:" << std::endl;
        for (size_t i = 0; i<fieldInfos.mergedAmpInfos.size(); i++) {
            const MergedDateAmplitudeType &val = fieldInfos.mergedAmpInfos[i];
            std::cout << i + 1 << " " <<
                         TimeToString(val.ttDate) << " " <<
                         ValueToString(val.vvInfo.meanVal) << " " <<
                         ValueToString(val.vvInfo.stdDev) << " " <<
                         ValueToString(val.vvInfo.weekNo) << " " <<
                         TimeToString(val.vvInfo.ttDateFloor) << " " <<

                         ValueToString(val.vhInfo.meanVal) << " " <<
                         ValueToString(val.vhInfo.stdDev) << " " <<
                         ValueToString(val.vhInfo.weekNo) << " " <<
                         TimeToString(val.vhInfo.ttDateFloor) << " " <<

                         ValueToString(val.ampRatio) << std::endl;
        }
    }

    void PrintAmpGroupedMeanValues(const std::vector<GroupedMeanValInfosType> &values) {
        std::cout << "Printing amplitude grouped mean values:" << std::endl;
        for (size_t i = 0; i<values.size(); i++) {
            const GroupedMeanValInfosType &val = values[i];
            std::cout << i + 1 << " " <<
                         TimeToString(val.ttDate) << " " <<
                         ValueToString(val.meanVal) << " " <<
                         ValueToString(val.maxVal) << " " <<
                         ValueToString(val.ampChange) << std::endl;
        }
    }

    void PrintNdviInfos(const FieldInfoType &fieldInfos) {
        std::cout << "Printing NDVI values:" << std::endl;
        for (size_t i = 0; i<fieldInfos.ndviLines.size(); i++) {
            const InputFileLineInfoType &val = fieldInfos.ndviLines[i];
            std::cout << i + 1 << " " <<
                         TimeToString(val.ttDate) << " " <<
                         ValueToString(val.meanVal) << " " <<
                         ValueToString(val.stdDev) << " " <<
                         ValueToString(val.weekNo) << " " <<
                         TimeToString(val.ttDateFloor) << std::endl;
        }
    }

    void PrintNdviGroupedMeanValues(const std::vector<GroupedMeanValInfosType> &values) {
        std::cout << "Printing NDVI Grouped mean values:" << std::endl;
        for (size_t i = 0; i<values.size(); i++) {
            const GroupedMeanValInfosType &val = values[i];
            std::cout << i + 1 << " " <<
                         TimeToString(val.ttDate) << " " <<
                         ValueToString(val.meanVal) << std::endl;
        }
    }

    void PrintCoherenceInfos(const FieldInfoType &fieldInfos) {
        std::cout << "Printing Coherence values:" << std::endl;
        for (size_t i = 0; i<fieldInfos.coheVVLines.size(); i++) {
            const InputFileLineInfoType &val = fieldInfos.coheVVLines[i];
            std::cout << i + 1 << " " <<
                         TimeToString(val.ttDate) << " " <<
                         TimeToString(val.ttDate2) << " " <<
                         ValueToString(val.meanVal) << " " <<
                         ValueToString(val.stdDev) << " " <<
                         ValueToString(val.weekNo) << " " <<
                         TimeToString(val.ttDateFloor) << " " <<
                         ValueToString(val.meanValChange) << " " <<
                         std::endl;
        }
    }

    void PrintCoherenceGroupedMeanValues(const std::vector<GroupedMeanValInfosType> &values) {
        std::cout << "Printing Coherence Grouped Mean values:" << std::endl;
        for (size_t i = 0; i<values.size(); i++) {
            const GroupedMeanValInfosType &val = values[i];
            std::cout << i + 1 << " " <<
                         TimeToString(val.ttDate) << " " <<
                         ValueToString(val.maxVal) << " " <<
                         ValueToString(val.maxChangeVal) << std::endl;
        }
    }

    void PrintHarvestEvaluation(const FieldInfoType &fieldInfo, HarvestInfoType &harvestInfo) {
        std::cout << "Printing Harvest Evaluation:" << std::endl;
        std::cout << "FIELD_ID COUNTRY YEAR MAIN_CROP VEG_START H_START H_END PRACTICE" << std::endl;
        std::cout << 0 << fieldInfo.fieldId << " " << fieldInfo.countryCode << " " << fieldInfo.mainCrop << " " <<
                     // VEG_START H_START H_END;"
                     TimeToString(fieldInfo.ttVegStartTime) << " " << TimeToString(fieldInfo.ttHarvestStartTime) << " " <<
                     // H_END PRACTICE
                     TimeToString(fieldInfo.ttHarvestEndTime) << " " << fieldInfo.practiceName << std::endl;

        std::cout << "P_TYPE P_START P_END M1 M2 M3 M4 M5 H_WEEK M6 M7 M8 M9" << std::endl;
        // "P_TYPE P_START P_END;
        std::cout << fieldInfo.practiceName << " " << fieldInfo.practiceType << " " << TimeToString(fieldInfo.ttPracticeStartTime) << " " <<
                     TimeToString(fieldInfo.ttPracticeEndTime) << " " <<
                    // M1;M2;
                    ValueToString(harvestInfo.evaluation.ndviPresence, true) << " " << ValueToString(harvestInfo.evaluation.candidateOptical, true) << " " <<
                    // M3;M4;
                    ValueToString(harvestInfo.evaluation.candidateAmplitude, true) << " " << ValueToString(harvestInfo.evaluation.amplitudePresence, true) << " " <<
                    // M5;H_WEEK;
                    ValueToString(harvestInfo.evaluation.candidateCoherence, true) << " " << ValueToString(harvestInfo.evaluation.harvestConfirmWeek) << " "
                  << std::endl;

        std::cout << "M10 C_INDEX" << std::endl;

        //                     // M6;M7;
        //                     efaHarvestInfo.evaluation.ndviPresence << ";" << efaHarvestInfo.evaluation.ndviGrowth << ";" <<
        //                     // M8;M9;
        //                     efaHarvestInfo.evaluation.ndviNoLoss << ";" << efaHarvestInfo.evaluation.ampNoLoss << ";" <<
        //                      //M10;C_INDEX
        //                     efaHarvestInfo.evaluation.cohNoLoss << ";" << efaHarvestInfo.evaluation.efaIndex

    }


    void PrintEfaMarkers(const std::vector<MergedAllValInfosType> &allMergedValues,
                         const std::vector<EfaMarkersInfoType> &efaMarkers) {
        std::cout << "Printing EFA Markers:" << std::endl;
        std::cout << "Group.1   coh.max coh.change grd.mean  grd.max ndvi.mean ndvi.presence" << std::endl;
        for (size_t i = 0; i<efaMarkers.size(); i++) {
            std::cout << i+1 << " " << TimeToString(allMergedValues[i].ttDate) <<  " " << ValueToString(allMergedValues[i].cohMax) <<  " "
                      << ValueToString(allMergedValues[i].cohChange) << " " << ValueToString(allMergedValues[i].ampMean) << " " <<
                         ValueToString(allMergedValues[i].ampMax) <<  " " << ValueToString(allMergedValues[i].ndviMeanVal) << " " <<
                         ValueToString(efaMarkers[i].ndviPresence, true)
                      << std::endl;
        }
        std::cout << "ndvi.drop ndvi.growth ndvi.noloss grd.noloss coh.noloss" << std::endl;
        for (size_t i = 0; i<efaMarkers.size(); i++) {
            std::cout << i + 1 << " " << ValueToString(efaMarkers[i].ndviDrop, true) << " " << ValueToString(efaMarkers[i].ndviGrowth, true) << " "  <<
                         ValueToString(efaMarkers[i].ndviNoLoss, true) << " "  << ValueToString(efaMarkers[i].ampNoLoss, true) << " "  <<
                         ValueToString(efaMarkers[i].cohNoLoss, true) << " "
                         << std::endl;
        }
    }



//    void TestBoostGregorian() {
//        time_t ttTime = FloorWeekDate(2012, 1);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2012, 2);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2012, 3);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2012, 4);
//        PrintTime(ttTime);

//        ttTime = FloorWeekDate(2015, 1);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2015, 2);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2015, 3);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2015, 4);
//        PrintTime(ttTime);

//        ttTime = FloorWeekDate(2016, 1);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2016, 2);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2016, 3);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2016, 4);
//        PrintTime(ttTime);
//        ttTime = FloorWeekDate(2016, 24);
//        PrintTime(ttTime);
//    }


//    void PrintTime(time_t ttTime) {
//        std::tm * ptm = std::localtime(&ttTime);
//        char buffer[32];
//        // Format: Mo, 15.06.2009 20:20:00
//        std::strftime(buffer, 32, "%a, %Y-%m-%d", ptm);
//        std::cout << "Formatted time_t: " << buffer << std::endl;
//    }

//    void TestComputeSlope() {
//        std::vector<std::string> xStr = {"2017-10-03", "2017-10-05", "2017-10-06", "2017-10-09", "2017-10-11", "2017-10-12",
//                                         "2017-10-15", "2017-10-17", "2017-10-18", "2017-10-21", "2017-10-23", "2017-10-24",
//                                         "2017-10-27", "2017-10-29", "2017-10-30", "2017-11-02", "2017-11-04"};
//        std::vector<double> y = {3.466435, 3.906034, 3.566496, 4.002097, 3.786935, 4.099542, 3.795383, 3.575887, 3.239278,
//                                 3.495796, 3.542520, 3.855183, 3.756771, 2.588200, 2.889339, 3.709943, 3.188229};
//        std::vector<double> x;
//        x.reserve(xStr.size());
//        std::transform(xStr.begin(), xStr.end(),
//                       std::back_inserter(x), [](std::string f){return GetTimeFromString(f) / SEC_IN_DAY;});
//        double slope;
//        ComputeSlope(x, y, slope);
//        std::vector<double> result = GetLinearFit(x, y);

//        // Compute the standard deviation
//        double meanVal = 0;
//        double stdDevVal = 0;
//        ComputeMeanAndStandardDeviation(x, meanVal, stdDevVal);
//        double stdErr = meanVal/std::sqrt(x.size());
//        std::cout << stdErr << std::endl;

//    }

private:
    std::unique_ptr<StatisticsInfosReaderBase> m_pAmpReader;
    std::unique_ptr<StatisticsInfosReaderBase> m_pNdviReader;
    std::unique_ptr<StatisticsInfosReaderBase> m_pCoheReader;

    std::unique_ptr<PracticeReaderBase> m_pPracticeReader;

    std::string m_outputDir;
    std::ofstream m_OutFileStream;
    std::string m_countryName;
    std::string m_practiceName;
    std::string m_year;

    std::ofstream m_OutPlotsFileStream;



    double m_OpticalThrVegCycle;
    // for MARKER 2 - NDVI loss
    // expected value of harvest/clearance
    double m_NdviDown;
    // buffer value (helps in case of sparse ndvi time-series)
    double m_NdviUp;
    // opt.thr.value is round up to ndvi.step
    double m_ndviStep;
    double m_OpticalThresholdMinimum;

    // for MARKER 5 - COHERENCE increase
    double m_CohThrBase;
    double m_CohThrHigh;
    double m_CohThrAbs;

    // for MARKER 3 - BACKSCATTER loss
    double m_AmpThrMinimum;

    // INPUT THRESHOLDS - EFA PRACTICE evaluation
    std::string m_CatchMain;
    // TODO: Ask Gisat for CZE
    int m_CatchPeriod;               // in days (e.g. 8 weeks == 56 days)
    double m_CatchProportion;       // buffer threshold
    std::string m_CatchPeriodStart;

    int m_EfaNdviThr;
    int m_EfaNdviUp;
    int m_EfaNdviDown;

    double m_EfaCohChange;
    double m_EfaCohValue;

    double m_EfaNdviMin;
    double m_EfaAmpThr;

    bool m_UseStdDevInAmpThrValComp = false;
    int m_OpticalThrBufDenominator = 6;

    std::string m_flMarkersStartDateStr;
    std::string m_flMarkersEndDateStr;

    std::string m_CatchCropVal;
    std::string m_FallowLandVal;
    std::string m_NitrogenFixingCropVal;

    // # optional: in case graphs shall be generated set the value to TRUE otherwise set to FALSE
    bool m_bPlotOutputGraph;
    // # optional: in case continuos products (csv file) shall be generated set the value to TRUE otherwise set to FALSE
    bool m_bResultContinuousProduct;

};

} // end of namespace Wrapper
} // end of namespace otb

OTB_APPLICATION_EXPORT(otb::Wrapper::TimeSeriesAnalysis)
