#include "config.h"
#include "netcdf.h"
#include "t_srcdir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The DDS in netcdf classic form is as follows: 
netcdf test {
dimensions:
	t = 10 ;
	x = 10 ;
	y = 10 ;
	z = 10 ;
variables:
	double OneD(x) ;
	double TwoD(x, y) ;
	double ThreeD(x, y, z) ;
	double FourD(x, y, z, t) ;
}
*/

/* Use the ThreeD variable */
#define VAR "ThreeD"

#define X 10
#define Y 10
#define Z 10

#define RANK 3

#define ERRCODE 2
#define ERR(e)                             \
  {                                        \
    printf("Error: %s\n", nc_strerror(e)); \
    exit(ERRCODE);                         \
  }

#undef DEBUG

/* Setup an odometer */
typedef struct Odom {
  int rank;
  size_t *index;
  size_t *stop;
  size_t *start;
  size_t *count;
} Odom;

#ifdef IGNORE
static float threeD_data[X * Y * Z];
static int dims[RANK] = {X, Y, Z};
#endif
static float threeD[X][Y][Z];

static Odom *odom_create(int rank);
static void odom_reclaim(Odom *odom);
static void odom_set(Odom *odom, size_t *start, size_t *count);
static int odom_more(Odom *odom);
static int odom_incr(Odom *odom);
static size_t odom_count(Odom *odom);

static int check(size_t *start, size_t *count);

/* Define whole variable start/count */
static size_t start0[RANK] = {0, 0, 0};
static size_t count0[RANK] = {X, Y, Z};

static int floateq(float f1, float f2) {
  float diff = f1 - f2;
  if (diff > 0 && diff < 0.005) return 1;
  if (diff < 0 && diff > -0.005) return 1;
  return 0;
}

int main() {
  int ncid, varid;
  int retval;
  size_t start[RANK];
  size_t count[RANK];
  const char *topsrcdir;
  char url[4096];

  topsrcdir = gettopsrcdir();

  strncpy(url, "file://", sizeof(url));
  strlcat(url, topsrcdir, sizeof(url));
  strlcat(url, "/ncdap_test/testdata3/test.06", sizeof(url));

  printf("test_vara: url=%s\n", url);

  memset((void *)threeD, 0, sizeof(threeD));

  if ((retval = nc_open(url, NC_NOWRITE, &ncid)))
    ERR(retval);

  if ((retval = nc_inq_varid(ncid, VAR, &varid)))
    ERR(retval);

  /* test 1: Read the whole variable */
  memcpy(start, start0, sizeof(start0));
  memcpy(count, count0, sizeof(count0));

  if ((retval = nc_get_vara_float(ncid, varid, start, count, (float *)threeD)))
    ERR(retval);
  if (!check(start, count)) goto fail;

  /* test 2: Read the slice with X=0 */
  memcpy(start, start0, sizeof(start0));
  memcpy(count, count0, sizeof(count0));
  count[0] = 1;

  if ((retval = nc_get_vara_float(ncid, varid, start, count, (float *)threeD)))
    ERR(retval);
  if (!check(start, count)) goto fail;

  if ((retval = nc_close(ncid)))
    ERR(retval);

  printf("*** PASS\n");
  return 0;
fail:
  printf("*** FAIL\n");
  return 1;
}

static int check(size_t *start, size_t *count) {
  int ok = 1;
  Odom *odom = odom_create(RANK);
  float *result = (float *)threeD;
  float *expected = (float *)threeD;
  odom_set(odom, start, count);
  while (odom_more(odom)) {
    size_t offset = odom_count(odom);
    if (floateq(result[offset], expected[offset])) {
      fprintf(stderr, "fail: result[%lu] = %f ; expected[%lu] = %f\n",
      (long unsigned)offset, result[offset], (long unsigned)offset, expected[offset]);
      ok = 0;
    }
    odom_incr(odom);
  }
  odom_reclaim(odom);
  return ok;
}

static Odom *odom_create(int rank) {
  Odom *odom = (Odom *)malloc(sizeof(Odom));
  /* Init the odometer */
  odom->rank = rank;
  odom->index = (size_t *)calloc(sizeof(size_t) * rank, 1);
  odom->stop = (size_t *)calloc(sizeof(size_t) * rank, 1);
  odom->start = (size_t *)calloc(sizeof(size_t) * rank, 1);
  odom->count = (size_t *)calloc(sizeof(size_t) * rank, 1);
  return odom;
}

static void odom_reclaim(Odom *odom) {
  free(odom->index);
  free(odom->stop);
  free(odom->start);
  free(odom->count);
  free(odom);
}

static void odom_set(Odom *odom, size_t *start, size_t *count) {
  int i;
  /* Init the odometer */
  for (i = 0; i < odom->rank; i++) {
    odom->start[i] = start[i];
    odom->count[i] = count[i];
  }
  for (i = 0; i < odom->rank; i++) {
    odom->index[i] = odom->start[i];
    odom->stop[i] = odom->start[i] + odom->count[i];
  }
}

static int odom_more(Odom *odom) {
  return (odom->index[0] < odom->stop[0] ? 1 : 0);
}

static int odom_incr(Odom *odom) {
  int i; /* do not make unsigned */
  if (odom->rank == 0) return 0;
  for (i = odom->rank - 1; i >= 0; i--) {
    odom->index[i]++;
    if (odom->index[i] < odom->stop[i]) break;
    if (i == 0) return 0;            /* leave the 0th entry if it overflows*/
    odom->index[i] = odom->start[i]; /* reset this position*/
  }
  return 1;
}

/* Convert current dapodometer settings to a single integer count*/
static size_t odom_count(Odom *odom) {
  int i;
  size_t offset = 0;
  for (i = 0; i < odom->rank; i++) {
    offset *= odom->count[i];
    offset += odom->index[i];
  }
  return offset;
}

#ifdef IGNORE
static float threeD_data[X][Y][Z] = {
1, 0.999950000416665, 0.999800006666578, 0.999550033748988,
0.999200106660978, 0.998750260394966, 0.998200539935204,
0.99755100025328, 0.996801706302619, 0.995952733011994,
0.995004165278026, 0.993956097956697, 0.992808635853866, 0.991561893714788,
0.990215996212637, 0.988771077936042, 0.987227283375627,
0.985584766909561, 0.983843692788121, 0.98200423511727,
0.980066577841242, 0.978030914724148, 0.975897449330606, 0.973666395005375,
0.97133797485203, 0.968912421710645, 0.966389978134513,
0.963770896365891, 0.961055438310771, 0.958243875512697,
0.955336489125606, 0.952333569885713, 0.949235418082441, 0.946042343528387,
0.942754665528346, 0.939372712847379, 0.935896823677935,
0.932327345606034, 0.92866463557651, 0.924909059857313,
0.921060994002885, 0.917120822816605, 0.913088940312308, 0.908965749674885,
0.904751663219963, 0.900447102352677, 0.896052497525525,
0.891568288195329, 0.886994922779284, 0.882332858610121,
0.877582561890373, 0.872744507645751, 0.86781917967765, 0.862807070514761,
0.857708681363824, 0.852524522059506, 0.847255111013416,
0.841900975162269, 0.836462649915187, 0.830940679100164,
0.825335614909678, 0.819648017845479, 0.813878456662534, 0.808027508312152,
0.802095757884293, 0.796083798549056, 0.789992231497365,
0.783821665880849, 0.777572718750928, 0.771246014997107,
0.764842187284488, 0.758361875990508, 0.751805729140895, 0.74517440234487,
0.738468558729588, 0.731688868873821, 0.724836010740905,
0.717910669610943, 0.710913538012277, 0.703845315652236,
0.696706709347165, 0.689498432951747, 0.682221207287614, 0.674875760071267,
0.667462825841308, 0.659983145884982, 0.652437468164052,
0.644826547240001, 0.63715114419858, 0.629412026573697,
0.621609968270664, 0.613745749488812, 0.605820156643463, 0.597833982287298,
0.589788025031098, 0.581683089463883, 0.573519986072457,
0.565299531160354, 0.557022546766217, 0.548689860581588,
0.54030230586814, 0.531860721374355, 0.52336595125165, 0.514818844969955,
0.506220257232778, 0.497571047891727, 0.488872081860527,
0.480124229028534, 0.47132836417374, 0.462485366875301,
0.453596121425577, 0.444661516741707, 0.435682446276712, 0.426659807930157,
0.417594503958358, 0.408487440884157, 0.399339529406273,
0.39015168430823, 0.380924824366882, 0.371659872260533,
0.362357754476674, 0.35301940121933, 0.343645746316047, 0.334237727124503,
0.324796284438776, 0.315322362395269, 0.305816908378289,
0.296280872925319, 0.286715209631956, 0.277120875056558,
0.267498828624587, 0.25785003253267, 0.248175451652373, 0.238476053433723,
0.228752807808459, 0.219006687093041, 0.209238665891419,
0.199449720997573, 0.189640831297834, 0.179812977673,
0.169967142900241, 0.160104311554831, 0.150225469911686, 0.140331605846737,
0.130423708738146, 0.120502769367367, 0.11056977982007,
0.100625733386932, 0.0906716244643097, 0.0807084484548006,
0.0707372016677029, 0.0607588812193859, 0.0507744849335792,
0.040785011241591, 0.0307914590824661, 0.0207948278030924,
0.0107961170582674, 0.000796326710733263, -0.00920354326880834,
-0.0192024929016926,
-0.0291995223012888, -0.0391936317729877, -0.0491838219141706,
-0.0591690937141481, -0.0691484486540619, -0.0791208888067339,
-0.089085416936459, -0.099041036598728, -0.108986752239871,
-0.118921569296612,
-0.128844494295525, -0.138754534952378, -0.148650700271364,
-0.158532000644198, -0.168397447949077, -0.178246055649492,
-0.18807683889288, -0.197888814609109, -0.207681001608784,
-0.217452420681365,
-0.227202094693087, -0.236929048684675, -0.246632309968834,
-0.256310908227523, -0.26596387560898, -0.275590246824513,
-0.285189059245021, -0.294759352997261, -0.304300171059833,
-0.313810559358882,
-0.323289566863503, -0.332736245680845, -0.342149651150898,
-0.35152884194096, -0.360872880139767, -0.370180831351287,
-0.379451764788155, -0.388684753364752, -0.397878873789916,
-0.407033206659266,
-0.416146836547142, -0.425218852098152, -0.4342483461183,
-0.443234415665709, -0.452176162140912, -0.461072691376713,
-0.469923113727602, -0.47872654415872, -0.487482102334359,
-0.496188912705999,
-0.504846104599858, -0.513452812303959, -0.522008175154707,
-0.530511337622945, -0.538961449399512, -0.547357665480271,
-0.555699146250613, -0.56398505756941, -0.572214570852437,
-0.580386863155222,
-0.588501117255346, -0.59655652173416, -0.60455227105793,
-0.612487565658385, -0.62036161201268, -0.628173622722739,
-0.635922816594002, -0.643608418713541, -0.651229660527546,
-0.658785779918188,
-0.666276021279824, -0.673699635594561, -0.681055880507152,
-0.688344020399238, -0.695563326462902, -0.702713076773554,
-0.70979255636212, -0.716801057286543, -0.723737878702569,
-0.730602326933837,
-0.737393715541245, -0.744111365391593, -0.750754604725491,
-0.757322769224544, -0.763815202077774, -0.770231254047307,
-0.776570283533293, -0.782831656638065, -0.789014747229531,
-0.795118937003784,
-0.801143615546934, -0.807088180396146, -0.81295203709989,
-0.818734599277382, -0.824435288677222, -0.830053535235222,
-0.835588777131408, -0.841040460846201, -0.846408041215776,
-0.851690981486566,
-0.856888753368947, -0.862000837090063, -0.867026721445802,
-0.871965903851917, -0.876817890394281, -0.881582195878286,
-0.886258343877352, -0.890845866780576, -0.895344305839492,
-0.899753211213941,
-0.904072142017061, -0.90830066635937, -0.912438361391958,
-0.916484813348769, -0.920439617587981, -0.924302378632464,
-0.928072710209333, -0.931750235288572, -0.935334586120739,
-0.938825404273736,
-0.942222340668658, -0.945525055614696, -0.948733218843107,
-0.951846509540242, -0.954864616379626, -0.95778723755309,
-0.960614080800952, -0.963344863441243, -0.965979312397975,
-0.968517164228447,
-0.970958165149591, -0.973302071063349, -0.975548647581083,
-0.977697670047013, -0.979748923560684, -0.981702202998454,
-0.983557313034006, -0.985314068157884, -0.986972292696038,
-0.988531820827396,
-0.989992496600445, -0.991354173948826, -0.992616716705937,
-0.993779998618556, -0.994843903359459, -0.995808324539061,
-0.996673165716047, -0.997438340407019, -0.998103772095146,
-0.998669394237814,
-0.999135150273279, -0.999500993626328, -0.999766887712928,
-0.999932805943894, -0.99999873172754, -0.999964658471342,
-0.999830589582598, -0.999596538468086, -0.999262528532721,
-0.998828593177219,
-0.998294775794753, -0.997661129766618, -0.996927718456887,
-0.996094615206081, -0.99516190332383, -0.994129676080546,
-0.992998036698093, -0.991767098339465, -0.990436984097473,
-0.989007826982433,
-0.987479769908865, -0.985852965681203, -0.984127576978514,
-0.982303776338232, -0.980381746138899, -0.978361678581934,
-0.97624377567241, -0.974028249198852, -0.971715320712062,
-0.969305221502961,
-0.966798192579461, -0.964194484642366, -0.961494358060299,
-0.958698082843669, -0.955805938617666, -0.952818214594305,
-0.949735209543496, -0.946557231763177, -0.943284599048476,
-0.939917638659938,
-0.936456687290796, -0.932902091033304, -0.929254205344123,
-0.925513395008784, -0.921680034105203, -0.917754505966276,
-0.913737203141545, -0.909628527357945, -0.90542888947963,
-0.901138709466889,
-0.896758416334147, -0.892288448107068, -0.88772925177875,
-0.883081283265026, -0.878345007358874, -0.873520897683938,
-0.868609436647165, -0.863611115390566, -0.858526433742102,
-0.8533559001657,
-0.848100031710408, -0.842759353958694, -0.83733440097388,
-0.831825715246746, -0.826233847641272, -0.820559357339561,
-0.814802811785913, -0.808964786630086, -0.803045865669731,
-0.797046640792012,
-0.790967711914417, -0.784809686924768, -0.778573181620433,
-0.772258819646744, -0.765867232434637, -0.759399059137508,
-0.752854946567295, -0.746235549129803, -0.739541528759258,
-0.73277355485212,
-0.72593230420014, -0.719018460922681, -0.71203271639831,
-0.704975769195658, -0.697848325003564, -0.690651096560507,
-0.683384803583336, -0.676050172695292, -0.668647937353351,
-0.66117883777488,
-0.653643620863612, -0.646043040134959, -0.63837785564066,
-0.630648833892775, -0.622856747787041, -0.615002376525574,
-0.607086505538955, -0.599109926407685, -0.591073436783031,
-0.582977840307259,
-0.574823946533269, -0.566612570843644, -0.55834453436911,
-0.550020663906425, -0.541641791835699, -0.533208756037154,
-0.524722399807346, -0.516183571774825, -0.507593125815277,
-0.49895192096614,
-0.490260821340699, -0.481520696041674, -0.47273241907431,
-0.46389686925898, -0.455014930143305, -0.446087489913793,
-0.437115441307028, -0.428099681520394, -0.419041112122356,
-0.409940638962306,
-0.400799172079975, -0.391617625614436, -0.38239691771268,
-0.373137970437818, -0.363841709676858, -0.354509065048132,
-0.345140969808323, -0.335738360759151, -0.326302178153684,
-0.316833365602319,
-0.307332869978419, -0.297801641323633, -0.288240632752882,
-0.278650800359055, -0.269033103117399, -0.259388502789626,
-0.249717963827731, -0.24002245327755, -0.230302940682059,
-0.220560397984419,
-0.21079579943078, -0.20101012147286, -0.191204342670302,
-0.181379443592811, -0.171536406722112, -0.161676216353687,
-0.151799858498356, -0.141908320783673, -0.13200259235517,
-0.122083663777433,
-0.112152526935055, -0.102210174933442, -0.0922576019995117,
-0.0822958033822624, -0.0723257752532545, -0.0623485146069917,
-0.0523650191612259, -0.0423762872571815, -0.0323833177597247,
-0.0223871099574771,
-0.0123886634628906, -0.00238897811228154, 0.0076109461341479,
0.0176101092923073, 0.0276075114542115, 0.0376021528879766,
0.0475930341377878, 0.057579156123846, 0.0675595202422752,
0.0775331284649787,
0.0874989834394464, 0.0974560885884857, 0.10740344820988,
0.117340067575955, 0.127264953033056, 0.137177112100907,
0.147075553571863, 0.156959287610023, 0.166827325850222, 0.176678681496857,
0.186512369422576, 0.196327406266778, 0.206122810533958, 0.215897602691854,
0.225650805269396, 0.235381442954451, 0.245088542691362,
0.254771133778243, 0.264428247964056, 0.274058919545427,
0.283662185463226, 0.293237085398863, 0.302782661870324, 0.312297960327916,
0.321782029249722, 0.331233920236754, 0.340652688107789,
0.350037390993891, 0.35938709043259, 0.368700851461733,
0.37797774271298, 0.387216836504937, 0.396417208935922, 0.405577939976361,
0.414698113560782, 0.423776817679428, 0.432813144469452,
0.441806190305705, 0.450755055891099, 0.459658846346532,
0.468516671300377, 0.477327644977522, 0.48609088628794, 0.494805518914805,
0.503470671402114, 0.512085477241841, 0.520649074960579,
0.529160608205695, 0.537619225830956, 0.546024081981648,
0.554374336179161, 0.562669153405032, 0.570907704184454, 0.57908916466921,
0.587212716720073, 0.595277547988606, 0.603282851998404,
0.611227828225735, 0.619111682179599, 0.626933625481169,
0.634692875942635, 0.642388657645414, 0.650020201017752, 0.657586742911669,
0.665087526679283, 0.672521802248466, 0.679888826197857,
0.687187861831201, 0.694418179251016, 0.701579055431586,
0.70866977429126, 0.715689626764061, 0.722637910870592, 0.729513931788232,
0.736317001920619, 0.74304644096641, 0.749701575987307,
0.756281741475356, 0.762786279419489, 0.769214539371333,
0.77556587851025, 0.781839661707619, 0.788035261590348, 0.794152058603611,
0.800189441072806, 0.806146805264716, 0.812023555447886,
0.817819103952194, 0.823532871227622, 0.829164285902202,
0.83471278483916, 0.840177813193225, 0.845558824466117, 0.850855280561193,
0.856066651837255, 0.861192417161521, 0.866232063961728,
0.871185088277397, 0.876050994810224, 0.880829296973609,
0.885519516941319, 0.890121185695265, 0.894633843072407, 0.899057037810768,
0.903390327594559, 0.907633279098413, 0.911785468030717,
0.915846479176035, 0.919815906436639, 0.92369335287311,
0.927478430744036, 0.931170761544783, 0.934769976045349, 0.938275714327283,
0.941687625819678, 0.945005369334228, 0.948228613099346,
0.951357034793342, 0.954390321576654, 0.957328170123131,
0.960170286650366, 0.962916386949075, 0.965566196411518, 0.968119450058955,
0.970575892568149, 0.972935278296897, 0.975197371308593,
0.977361945395819, 0.979428784102971, 0.981397680747901,
0.983268438442584, 0.985040870112812, 0.986714798516892, 0.98829005626338,
0.989766485827815, 0.991143939568469, 0.992422279741117,
0.993601378512806, 0.994681117974643, 0.99566139015358,
0.996542097023217, 0.997323150513601, 0.998004472520033, 0.998585994910881,
0.99906765953439, 0.999449418224499, 0.999731232805658,
0.999913075096642, 0.999994926913375, 0.999976780070743,
0.999858636383415, 0.999640507665662, 0.999322415730172, 0.998904392385876,
0.998386479434759, 0.997768728667684, 0.997051201859214,
0.996233970761431, 0.995317117096764, 0.994300732549815,
0.993184918758193, 0.991969787302346, 0.990655459694407, 0.989242067366043,
0.987729751655308, 0.986118663792513, 0.984408964885101,
0.982600825901538, 0.980694427654217, 0.978689960781373,
0.976587625728023, 0.974387632725921, 0.972090201772533, 0.96969556260904,
0.967203954697364, 0.964615627196218, 0.961930838936196,
0.959149858393887, 0.956272963665028, 0.953300442436693,
0.95023259195853, 0.947069719013028, 0.943812139884847, 0.940460180329185,
0.937014175539204, 0.933474470112512, 0.929841418016701,
0.926115382553955, 0.922296736324713, 0.918385861190416,
0.914383148235319, 0.910288997727383, 0.906103819078245, 0.901828030802283,
0.897462060474762, 0.893006344689077, 0.888461329013091,
0.883827467944587, 0.879105224865808, 0.874295071997128,
0.869397490349825, 0.864412969677983, 0.859342008429514, 0.854185113696319,
0.848942801163572, 0.84361559505816, 0.838204028096251,
0.832708641430035, 0.827129984593597, 0.821468615447972,
0.815725100125357, 0.809900012972498, 0.803993936493257, 0.798007461290359,
0.791941186006336, 0.785795717263661, 0.779571669604088,
0.773269665427194, 0.766890334928147, 0.760434316034681,
0.753902254343305, 0.747294803054744, 0.740612622908621, 0.733856382117381,
0.727026756299476, 0.720124428411794, 0.713150088681373,
0.706104434536373, 0.698988170536338, 0.691802008301737,
0.684546666442807, 0.677222870487685, 0.669831352809865, 0.662372852554955,
0.654848115566766, 0.647257894312724, 0.639602947808631,
0.631884041542758, 0.624101947399299, 0.616257443581182,
0.608351314532255, 0.600384350858831, 0.592357349250641, 0.584271112401154,
0.576126448927319, 0.567924173288695, 0.55966510570601,
0.551350072079141, 0.542979903904521, 0.534555438191992,
0.526077517381105, 0.517546989256877, 0.50896470686501, 0.500331528426593,
0.491648317252275, 0.482915941655938, 0.474135274867862,
0.465307194947413, 0.456432584695223, 0.447512331564922,
0.43854732757439, 0.429538469216557, 0.420486657369749, 0.411392797207609,
0.402257798108573, 0.393082573564941, 0.38386804109152,
0.374615122133879, 0.365324741976202, 0.355997829648764,
0.346635317835026, 0.337238142778366, 0.327807244188458, 0.318343565147303,
0.30884805201492, 0.299321654334707, 0.289765324738495,
0.280180018851278, 0.27056669519566, 0.260926315095994,
0.251259842582256, 0.241568244293641, 0.231852489381904, 0.222113549414439,
0.212352398277126, 0.202570012076944, 0.192767369044364,
0.182945449435517, 0.173105235434182, 0.163247711053556,
0.153373862037864, 0.14348467576378, 0.13358114114169, 0.123664248516802,
0.113734989570117, 0.103794357219253, 0.0938433455191623,
0.0838829495627223, 0.0739141653812273, 0.06393798984479,
0.0539554205626498, 0.0439674557834159, 0.0339750942952423,
0.0239793353259525, 0.0139811784431128, 0.00398162345407974,
-0.0060183296939816, -0.0160176810140879, -0.0260154305794408,
-0.0360105786234153,
-0.0460021256395369, -0.0559890724814288, -0.0659704204627299,
-0.0759451714569599, -0.0859123279973325, -0.0958708933764978,
-0.105819871746218, -0.115758268216946, -0.125685088957318,
-0.135599341293531,
-0.145500033808614, -0.155386176441565, -0.16525678058636,
-0.17511085919081, -0.184947426855267, -0.194765499931161,
-0.204564096619365, -0.214342237068377, -0.2240989434723,
-0.233833240168624,
-0.243544153735791, -0.253230713090538, -0.262891949585,
-0.272526897103582, -0.282134592159557, -0.291714073991427,
-0.301264384658992, -0.310784569139144, -0.320273675421368,
-0.329730754602944,
-0.339154860983835, -0.348545052161256, -0.357900389123914,
-0.367219936345908, -0.376502761880283, -0.385747937452222,
-0.394954538551871, -0.404121644526792, -0.413248338674028,
-0.422333708331768,
-0.431376844970621, -0.440376844284454, -0.449332806280839,
-0.458243835371038, -0.467109040459569, -0.47592753503331,
-0.484698437250152, -0.493420870027184, -0.502093961128397,
-0.510716843251906,
-0.519288654116686, -0.527808536548793, -0.536275638567084,
-0.544689113468413, -0.553048119912302, -0.561351822005071,
-0.569599389383433, -0.57778999729752, -0.585922826693367,
-0.593997064294812,
-0.602011902684824, -0.609966540386242, -0.617860181941925,
-0.625692037994295, -0.633461325364275, -0.641167267129602,
-0.648809092702519, -0.656386037906838, -0.663897345054353,
-0.671342263020609,
-0.678720047320012, -0.686029960180282, -0.693271270616224,
-0.700443254502829, -0.707545194647683, -0.714576380862692,
-0.721536110035093, -0.728423686197768, -0.735238420598841,
-0.741979631770551,
-0.748646645597399, -0.755238795383558, -0.76175542191954,
-0.768195873548125, -0.774559506229517, -0.780845683605749,
-0.787053777064324, -0.793183165801068, -0.799233236882215,
-0.8052033853057,
-0.811093014061656, -0.816901534192113, -0.8226283648499,
-0.828272933356724, -0.833834675260437, -0.839313034391484,
-0.844707462918517, -0.850017421403178, -0.855242378854046,
-0.860381812779727,
-0.865435209241112, -0.870402062902767, -0.875281877083464,
-0.880074163805853, -0.884778443845253, -0.889394246777581,
-0.893921111026392, -0.898358583909032, -0.90270622168191,
-0.906963589584872,
-0.911130261884677, -0.915205821917566, -0.919189862130932,
-0.923081984124074, -0.926881798688036, -0.930588925844528,
-0.934202994883924, -0.937723644402332, -0.941150522337732,
-0.944483286005189,
-0.947721602131112, -0.950865146886587, -0.953913605919758,
-0.956866674387264, -0.959724056984716, -0.962485467976237,
-0.965150631223029, -0.967719280210989, -0.970191158077357,
-0.972566017636408,
-0.974843621404164, -0.977023741622146, -0.97910616028015,
-0.981090669138045, -0.982977069746599, -0.984765173467324,
-0.986454801491336, -0.988045784857242, -0.989537964468031,
-0.990931191106986,
-0.992225325452603, -0.993420238092527, -0.994515809536489,
-0.995511930228257, -0.996408500556594, -0.997205430865212,
-0.997902641461745, -0.998500062625715, -0.998997634615504,
-0.999395307674325,
-0.999693042035206, -0.999890807924959, -0.999988585567158,
-0.999986365184122, -0.999884146997886, -0.999681941230185,
-0.999379768101426, -0.998977657828671, -0.998475650622611,
-0.99787379668355,
-0.997172156196378, -0.996370799324562, -0.995469806203119,
-0.994469266930611, -0.993369281560131, -0.992169960089301,
-0.990871422449267, -0.989473798492712, -0.987977227980866,
-0.986381860569534,
-0.984687855794127, -0.982895383053711, -0.981004621594066,
-0.979015760489763, -0.976928998625255, -0.974744544674989,
-0.97246261708254, -0.970083444038766, -0.967607263458988,
-0.965034322959201,
-0.96236487983131, -0.959599201017404, -0.95673756308306,
-0.953780252189686, -0.950727564065908, -0.947579803977993,
-0.944337286699328, -0.941000336478938, -0.937569287009064,
-0.934044481391795,
-0.930426272104753, -0.926715020965855, -0.922911099097119,
-0.919014886887564, -0.915026773955164, -0.910947159107888,
-0.906776450303821, -0.902515064610368, -0.898163428162546,
-0.893721976120377,
-0.889191152625361, -0.884571410756073, -0.879863212482849,
-0.875067028621594, -0.870183338786697, -0.865212631343072,
-0.86015540335732, -0.855012160548026, -0.849783417235186,
-0.844469696288772};
#endif
