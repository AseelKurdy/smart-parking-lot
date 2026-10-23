// ============ Smart Parking System v9 - A* Assignment + תמונת החניון ============
// מגישים: Aseel Kurdy, Nayef Habiballah, Osama Abdulhalim   |   מנחה: עידו
// החניון: דגם בקנה מידה 1:100 שנמדד בפועל - כל המרחקים והמידות בס"מ של הדגם (סעיף [2])
//
// פקודות ב-Serial Monitor:
//   new                  רכב חדש נכנס, המערכת משריינת לו את החניה הקרובה הפנויה
//   park <spot> <id>     רכב עם המזהה id חונה בחניה spot
//   leave <id>           הרכב עם המזהה id יוצא מהחניון כולו (חנה או עדיין בדרך)
//   cancel <id>          זהה ל-leave
//   status               מרענן את התמונה הגרפית של החניון (שורת סיכום ב-Serial, התמונה בדפדפן)
//   check [sec]          מרחקי החיישנים למשך sec שניות (ברירת מחדל 3)
//   help                 רשימת הפקודות
//
// הפקודות לא תלויות באותיות גדולות/קטנות (NEW = new).
//
// לכל חניה שלושה מצבים: free (פנויה) / RESERVED (משוריינת לרכב בדרך) /
// OCCUPIED (רכב חונה בה בפועל). לחניה 0 יש מצב רביעי: BLOCKED - החיישן רואה בה
// עצם בלי רכב רשום, ולכן לא שולחים אליה רכבים עד שהיא מתפנה.
// שריון שלא מומש תוך RESERVATION_TIMEOUT_MS משתחרר אוטומטית.
// חניה 0 פיזית: החיישן מזהה גם הגעה וגם יציאה (יציאה = הרכב עזב את החניון).
//
// ===== v9: תמונה גרפית של החניון - איפה כל חלק רץ =====
//   1. ה-ESP32 (הקובץ הזה): מחשב את מצב החניון ומרכיב אותו כ-JSON (buildStateJson, סעיף [4ב]).
//      הוא לא מצייר שום דבר, רק מספק נתונים.
//   2. HTTP על ה-ESP32 (סעיף [7ב]): GET /state מחזיר את ה-JSON, GET / מחזיר את העמוד.
//   3. הדפדפן (טלפון/מחשב): ה-JavaScript בקובץ parking_page.h לוקח את ה-JSON כל שנייה
//      ומצייר ממנו את התמונה (SVG): החניות, הרכבים החונים, ורשימת הרכבים שממתינים בצד.
//      *** הציור עצמו קורה שם, לא ב-ESP32 ולא ב-Serial Monitor. ***
//   4. פקודת status: מגדילה את statusSeq. הדפדפן רואה שהמספר השתנה ומרענן את התמונה
//      מיד (עם הודעה קצרה), וב-Serial מודפסת רק שורת סיכום.
//   5. אם ה-WiFi כבוי או לא עלה (ENABLE_WEB_VIEW 0), ה-Serial Monitor חוזר לציור טקסט (printLotMap).
// בלי התקנות בצד המחשב ובלי חיבור לחיישנים נוספים: הדפדפן הוא "חלון התמונה".
// parking_page.h חייב להישאר באותה תיקייה של ה-.ino
//
// חיישן הכניסה (פין 27/14): מביט לרוחב הכביש בכניסה. כביש ריק ~30 ס"מ (הקיר שמולו), רכב ~22 ס"מ;
// רכב נספר כשהקריאה מתחת ל-25 ס"מ.
// רכב שנראה = רכב חדש (כמו new). אפשר להגדיר באותו חיישן גם נתיב יציאה נפרד במרחק אחר
// (GATE_EXIT_CM; כבוי כברירת מחדל - היציאה נקלטת בחיישן חניה 0 או בפקודת leave)

// ==================== [1] חומרה וחיישנים ====================
const int trigPin1 = 27, echoPin1 = 14;   // חיישן הכניסה (מהצד)
const int trigPin2 = 26, echoPin2 = 25;   // חיישן החניה הפיזית (מלמעלה, מעל חניה 0)
const int ledPin   = 2;                   // LED כחול מובנה

// חיישן הכניסה - המרחקים שנמדדו על הדגם (כביש ריק 30 / רכב 22).
// רכב מזוהה כשהקריאה מתחת ל-GATE_CAR_BELOW_CM; קריאה מעליו = כביש ריק
const float GATE_EMPTY_CM     = 30.0;   // כביש ריק: הקיר שמול החיישן (גם "אין הד" = ריק)
const float GATE_ENTRY_CM     = 22.0;   // רכב שנכנס לחניון (נמדד)
const float GATE_CAR_BELOW_CM = 25.0;   // סף הזיהוי: קריאה מתחת לזה = יש רכב בכניסה
const float GATE_EXIT_CM      = 0.0;    // רכב בנתיב יציאה נפרד מול אותו חיישן; 0 = אין נתיב כזה
                                        // (אם מוגדר: רכב מתחת לסף משויך לקרוב מבין ENTRY / EXIT)
const int   GATE_CONFIRM_SAMPLES = 2;   // כמה דגימות רצופות באותו נתיב מאשרות רכב (רכב עקמומי קופץ בין ערכים)
const int   GATE_CLEAR_SAMPLES   = 2;   // כמה דגימות "ריק" רצופות צריך לפני שרכב חדש נספר
const unsigned long EXIT_MATCH_MS = 20000;   // רכב שיצא מחניה 0 ואחר כך נראה בנתיב היציאה הוא אותו רכב
// חניה 0 (חיישן מלמעלה) - נמדד: ריק 25-26 ס"מ, רכב בגובה 2.5 ס"מ: 22.5-23.5 ס"מ.
// הספים מותאמים גם לרכב נמוך בגובה 1.5 ס"מ: הוא קורא 23.5-24.5 ס"מ.
// היסטרזיס: רכב מגיע כשהקריאה יורדת מתחת ל-ARRIVE, ויוצא כשהיא עולה מעל LEAVE.
// בין 24.5 (הרכב הנמוך, הקריאה הרחוקה ביותר) ל-25.0 (הריק הקרוב ביותר) יש רק 0.5 ס"מ,
// אז נשאר 0.2 ס"מ מצד הרכב ו-0.1 ס"מ מצד הריק - צר מאוד, ברמת הדיוק של HC-SR04.
// אם יש קפיצות, לבדוק עם check ולהזיז את הספים (או להרים את החיישן כדי שהריק יקרא רחוק יותר)
const float SPOT_ARRIVE_CM = 24.7;
const float SPOT_LEAVE_CM  = 24.9;
const float MIN_VALID_CM   = 2.0;   // טווח המינימום של HC-SR04 - קריאה קצרה יותר היא רעש

const unsigned long SAMPLE_INTERVAL_MS = 500;   // כל כמה זמן נדגמים החיישנים
const int CHECK_DEFAULT_S      = 3;             // אורך ברירת המחדל של פקודת check
const int CROSSTALK_DELAY_MS   = 60;            // מניעת הפרעה בין החיישנים (60ms לפי ה-datasheet)
const int SPOT_CONFIRM_SAMPLES = 4;             // 4 דגימות רצופות (2 שניות) לשינוי מצב החניה הפיזית
const unsigned long BLINK_INTERVAL_MS = 300;    // הבהוב כשחנה בחניה הלא נכונה

const unsigned long RESERVATION_TIMEOUT_MS = 60000;   // שריון שלא מומש משתחרר

// ==================== [1ב] תמונת החניון ====================
// התמונה הגרפית: עמוד אינטרנט שה-ESP32 מגיש. הוא פותח רשת WiFi משלו (בלי סיסמה
// ובלי אינטרנט), מתחברים אליה מטלפון/מחשב ופותחים את הכתובת שמודפסת ב-[WEB].
// העמוד רק מציג, אי אפשר לשלוט דרכו בחניון. 0 = בלי WiFi (נשאר רק ציור הטקסט)
#define ENABLE_WEB_VIEW 1
const char* WEB_SSID = "SmartParking";

// ציור הטקסט ב-Serial Monitor הוא רק גיבוי: ברירת המחדל שלו מופיע כשאין עמוד גרפי
// (WiFi כבוי או לא עלה). true = מצייר אותו גם כשהעמוד פועל.
// ב-9600 baud ציור אחד לוקח כשנייה להדפסה, ובזמן הזה הלולאה עומדת
const bool TEXT_MAP = false;

bool webRunning = false;   // true רק אחרי שה-WiFi והשרת עלו (מוגדר ב-startWebView)

// מתי להדפיס את ציור הטקסט
bool showTextMap() { return TEXT_MAP || !webRunning; }

unsigned long statusSeq = 0;   // עולה בכל פקודת status; העמוד מזהה את השינוי ומתרענן מיד

#if ENABLE_WEB_VIEW
#include <WiFi.h>
#include <WebServer.h>
#include "parking_page.h"
#endif

// ==================== [2] הדגם: מידות ומרחקים שנמדדו (ס"מ) ====================
// מבנה הדגם (מבט מלמעלה, כמו בתמונה): הכביש הראשי נכנס מלמטה. קומה 0 משמאל לימין - החניות
// לאורך הקיר הימני, חניה 0 הכי קרובה לכניסה. הרמפה מתחילה על הכביש הראשי *לפני* חניות קומה 0,
// עולה ומסתובבת (U) אל קומה 1 - המשטח המוגבה משמאל, החניות לאורך צדו השמאלי, חניה 7 ראשונה.
// קנה מידה 1:100: חניה של 2.5x5 ס"מ בדגם = חניה סטנדרטית של 2.5x5 מ'. לכן ס"מ בדגם = מ' במציאות.
const float ENT_TO_FLOOR0_CM   = 22.0;  // כניסה -> נתיב קומה 0, ממש לפני החניה הראשונה (חניה 0)
const float ENT_TO_RAMP_CM     = 17.5;  // כניסה -> תחילת הרמפה
const float RAMP_CM            = 20.0;  // אורך הרמפה: מתחילתה עד הכניסה לקומה 1
const float FLOOR1_TO_FIRST_CM = 7.5;   // כניסה לקומה 1 -> נתיב קומה 1, ממש לפני החניה הראשונה (חניה 7)
const float SPOT_WIDTH_CM      = 2.5;   // רוחב חניה = הצעד לאורך הנתיב מחניה לחניה
const float SPOT_LENGTH_CM     = 5.0;   // אורך חניה = מהנתיב עד שהרכב עומד בתוכה
const float CAR_LENGTH_CM = 4.0, CAR_WIDTH_CM = 2.0, CAR_HEIGHT_CM = 2.5;   // רכב הדגם (לתמונה ולתיעוד)
const char* DIST_UNIT = "cm";            // יחידת המרחקים בפלט (ס"מ של הדגם)
// התוצאה: קומה 0 - 27.0, 29.5 ... 42.0 ס"מ; קומה 1 - 50.0, 52.5 ... 65.0 ס"מ

const int FLOORS = 2;
const int SPOTS_PER_FLOOR = 7;   // חניות 0-6 בקומה 0, חניות 7-13 בקומה 1, כל קומה לפי הסדר מהכניסה שלה

// ==================== [3] טבלת החניות והרכבים ====================
const int TOTAL_SPOTS = 14;
const uint8_t SPOT_FL[TOTAL_SPOTS] = {0,0,0,0,0,0,0, 1,1,1,1,1,1,1};
int spotIndexOnFloor(int i) { return i % SPOTS_PER_FLOOR; }   // 0 = הראשונה מכניסת הקומה

const int REAL_SPOT_INDEX = 0;   // החניה עם החיישן וה-LED הפיזיים

float spotDist[TOTAL_SPOTS];             // מרחק A* מהכניסה, מחושב פעם אחת ב-setup
bool  occupied[TOTAL_SPOTS];             // רכב חונה בפועל
int   assignedCarId[TOTAL_SPOTS];        // >=0 => משוריינת לרכב הזה
unsigned long reservedAtMs[TOTAL_SPOTS]; // מתי נוצר השריון
int   parkedCarId[TOTAL_SPOTS];          // מי חונה בפועל
bool  wasCorrect[TOTAL_SPOTS];
bool  spotSensedStable = false;          // מצב החיישן של חניה 0 אחרי סינון רעשים

const int MAX_CARS = 500;
bool carExited[MAX_CARS];   // הרכב יצא מהחניון
int  nextCarId = 0;         // הרכב הראשון מקבל ID 0

// ==================== [4] רשת הדרכים ו-A* ====================
// רשת הדרכים של הדגם: צמתים וקטעי דרך באורך שנמדד (ס"מ). נבנית מהקבועים של סעיף [2]:
//   כניסה --17.5--> תחילת רמפה --20--> כניסה לקומה 1 --7.5--> נתיב קומה 1 (מול חניה 7) ...
//   כניסה --22--> נתיב קומה 0 (מול חניה 0) --2.5--> מול חניה 1 --2.5--> ...
//   מכל נקודה בנתיב --5--> לתוך החניה שמולה
const int NODE_ENT    = 0;                          // הכניסה הראשית
const int NODE_RAMP   = 1;                          // תחילת הרמפה
const int NODE_FLOOR1 = 2;                          // הכניסה לקומה 1 (ראש הרמפה)
const int NODE_LANE0  = 3;                          // 3..16:  הנקודה בנתיב מול חניה i
const int NODE_SPOT0  = NODE_LANE0 + TOTAL_SPOTS;   // 17..30: בתוך חניה i
const int NODE_COUNT  = NODE_SPOT0 + TOTAL_SPOTS;
const int MAX_EDGES   = 40;
uint8_t edgeA[MAX_EDGES], edgeB[MAX_EDGES];
float   edgeLen[MAX_EDGES];
int     edgeCount = 0;

void addEdge(int a, int b, float len) {
  if (edgeCount >= MAX_EDGES) return;
  edgeA[edgeCount] = a; edgeB[edgeCount] = b; edgeLen[edgeCount] = len; edgeCount++;
}

void buildRoadNetwork() {
  edgeCount = 0;
  addEdge(NODE_ENT,    NODE_LANE0,                   ENT_TO_FLOOR0_CM);
  addEdge(NODE_ENT,    NODE_RAMP,                    ENT_TO_RAMP_CM);
  addEdge(NODE_RAMP,   NODE_FLOOR1,                  RAMP_CM);
  addEdge(NODE_FLOOR1, NODE_LANE0 + SPOTS_PER_FLOOR, FLOOR1_TO_FIRST_CM);
  for (int i=0;i<TOTAL_SPOTS;i++) {
    if (spotIndexOnFloor(i) < SPOTS_PER_FLOOR-1)
      addEdge(NODE_LANE0+i, NODE_LANE0+i+1, SPOT_WIDTH_CM);   // לאורך הנתיב, לחניה הבאה
    addEdge(NODE_LANE0+i, NODE_SPOT0+i, SPOT_LENGTH_CM);       // מהנתיב לתוך החניה
  }
}

// היוריסטיקה: חסם תחתון על המרחק שנשאר עד חניה goalSpot. כל מסלול מסתיים בנסיעה של
// SPOT_LENGTH_CM לתוך החניה; ומנקודה על הנתיב של אותה קומה צריך לנסוע לפחות לאורך הנתיב
// עד מול החניה. היא אף פעם לא מעריכה ביתר (קבילה), ולכן A* מחזיר את המסלול הקצר ביותר
float heuristic(int node, int goalSpot) {
  if (node == NODE_SPOT0 + goalSpot) return 0;
  if (node >= NODE_LANE0 && node < NODE_SPOT0) {
    int s = node - NODE_LANE0;
    if (SPOT_FL[s] == SPOT_FL[goalSpot])
      return abs(spotIndexOnFloor(s) - spotIndexOnFloor(goalSpot)) * SPOT_WIDTH_CM + SPOT_LENGTH_CM;
  }
  return SPOT_LENGTH_CM;
}

// המרחק הקצר ביותר מהכניסה עד בתוך חניה goalSpot (ס"מ), או -1 אם אין דרך
float aStarDistance(int goalSpot) {
  static float g[NODE_COUNT];
  static bool  inOpen[NODE_COUNT];
  for (int n=0;n<NODE_COUNT;n++){ g[n]=1e9; inOpen[n]=false; }
  const int goal = NODE_SPOT0 + goalSpot;
  g[NODE_ENT]=0; inOpen[NODE_ENT]=true;

  while (true) {
    int best=-1; float bestF=1e9;
    for (int n=0;n<NODE_COUNT;n++) {
      if (!inOpen[n]) continue;
      float f=g[n]+heuristic(n, goalSpot);
      if (f<bestF){bestF=f;best=n;}
    }
    if (best==-1) return -1;
    if (best==goal) return g[best];
    inOpen[best]=false;

    for (int e=0;e<edgeCount;e++) {
      int other;
      if (edgeA[e]==best) other=edgeB[e];
      else if (edgeB[e]==best) other=edgeA[e];
      else continue;
      if (other>=NODE_SPOT0 && other!=goal) continue;   // אסור לנסוע דרך חניה
      float t=g[best]+edgeLen[e];
      if (t<g[other]){g[other]=t;inOpen[other]=true;}
    }
  }
}

// הדגם קבוע, לכן מספיק להריץ A* פעם אחת לכל חניה
void computeSpotDistances() {
  buildRoadNetwork();
  for (int i=0;i<TOTAL_SPOTS;i++) spotDist[i] = aStarDistance(i);
}

// חניה 0 חסומה = החיישן רואה בה עצם, אבל אף רכב רשום לא חונה בה
bool spot0Blocked() {
  return spotSensedStable && !occupied[REAL_SPOT_INDEX];
}

// חניה זמינה = לא תפוסה, לא משוריינת לרכב אחר ולא חסומה פיזית
int findAvailableSpot(float *outDist) {
  int best=-1; float bestD=1e9;
  for (int i=0;i<TOTAL_SPOTS;i++) {
    if (occupied[i]) continue;
    if (assignedCarId[i] >= 0) continue;
    if (i == REAL_SPOT_INDEX && spot0Blocked()) continue;
    float d=spotDist[i];
    if (d>=0 && d<bestD){bestD=d;best=i;}
  }
  if (outDist)*outDist=bestD;
  return best;
}

int whereIsCarParked(int carId) {
  for (int i=0;i<TOTAL_SPOTS;i++)
    if (occupied[i] && parkedCarId[i]==carId) return i;
  return -1;
}

int whereIsCarReserved(int carId) {
  for (int i=0;i<TOTAL_SPOTS;i++)
    if (assignedCarId[i]==carId) return i;
  return -1;
}

void clearReservationFor(int carId) {
  for (int i=0;i<TOTAL_SPOTS;i++)
    if (assignedCarId[i]==carId) assignedCarId[i]=-1;
}

// רכב בחניון = נכנס ועדיין לא יצא
bool isCarInLot(int carId) {
  return carId >= 0 && carId < nextCarId && !carExited[carId];
}

// הרכב האחרון שנכנס ועדיין לא חנה (לשיוך רכב שהגיע לחניה הפיזית)
int newestUnparkedCar() {
  for (int id=nextCarId-1; id>=0; id--)
    if (!carExited[id] && whereIsCarParked(id) < 0) return id;
  return -1;
}

// ==================== [4ב] מצב החניות וציורן ====================
const int SP_FREE = 0, SP_RESERVED = 1, SP_OCCUPIED = 2, SP_BLOCKED = 3;

// המצב של חניה אחת. הסדר קובע: רכב חונה גובר על שריון, ושריון על חסימה
int spotState(int i) {
  if (occupied[i]) return SP_OCCUPIED;
  if (assignedCarId[i] >= 0) return SP_RESERVED;
  if (i == REAL_SPOT_INDEX && spot0Blocked()) return SP_BLOCKED;
  return SP_FREE;
}

// כמה שניות נשארו עד שהשריון של החניה פג
unsigned long reservationSecondsLeft(int i) {
  unsigned long age = millis() - reservedAtMs[i];
  return (age >= RESERVATION_TIMEOUT_MS) ? 0 : (RESERVATION_TIMEOUT_MS - age) / 1000;
}

// רכבים שמחכים: נכנסו לחניון ועדיין לא חנו. לכל אחד יש חניה מיועדת (whereIsCarReserved),
// או שאין לו כבר כזו אם השריון שלו פג. שומר ב-ids את המזהים הראשונים (לפי סדר ההגעה)
// ומחזיר כמה רכבים מחכים בסך הכול
int collectWaitingCars(int* ids, int maxIds) {
  int total = 0;
  for (int id=0; id<nextCarId; id++) {
    if (carExited[id] || whereIsCarParked(id) >= 0) continue;
    if (total < maxIds) ids[total] = id;
    total++;
  }
  return total;
}

// ציור הטקסט מחקה את הדגם במבט מלמעלה: קומה 1 (המשטח המוגבה) משמאל, הרמפה באמצע,
// קומה 0 מימין, הכניסה למטה. כל קומה: עמודת חניות (MAP_CELL_W) ועמודת נתיב (MAP_LANE_W)
const int MAP_LANE_W = 5;
const int MAP_CELL_W = 17;   // 5 תווים למספר החניה + 12 לתיאור המצב
const int MAP_RAMP_W = 9;
const int MAP_SIDE_W = MAP_CELL_W + MAP_LANE_W + 3;   // בלוק של קומה אחת כולל 3 קווי הגבול

void printRepeat(char ch, int n) { for (int k=0;k<n;k++) Serial.print(ch); }

void printCentered(const char* s, int width) {
  int len = strlen(s);
  if (len > width) len = width;
  int left = (width - len) / 2;
  printRepeat(' ', left);
  for (int k=0;k<len;k++) Serial.print(s[k]);
  printRepeat(' ', width - left - len);
}

// תא חניה: מספר החניה (* = החניה הפיזית) ומצבה. הרכב החונה מצויר כ-[#מזהה OK/WRONG]
void printSpotCell(int spot, int nextSpot) {
  char label[8], txt[16];
  snprintf(label, sizeof(label), " %2d%c ", spot, spot == REAL_SPOT_INDEX ? '*' : ' ');
  switch (spotState(spot)) {
    case SP_OCCUPIED:
      snprintf(txt, sizeof(txt), "[#%d %s]", parkedCarId[spot], wasCorrect[spot] ? "OK" : "WRONG");
      break;
    case SP_RESERVED:
      snprintf(txt, sizeof(txt), "RES #%d %lus", assignedCarId[spot], reservationSecondsLeft(spot));
      break;
    case SP_BLOCKED:
      snprintf(txt, sizeof(txt), "//BLOCKED//");
      break;
    default:
      snprintf(txt, sizeof(txt), "%s", spot == nextSpot ? ">> NEXT <<" : "free");
  }
  Serial.print(label);
  printCentered(txt, MAP_CELL_W - 5);
}

// שורה r (0 = למעלה): משמאל חניה 7+r בקומה 1 (חניה 7 ליד ראש הרמפה), באמצע הרמפה,
// מימין חניה 6-r בקומה 0 (חניה 0 למטה, ליד הכניסה). החצים = כיוון הנסיעה בנתיב
void printMapRow(int r, int nextSpot) {
  Serial.print('|'); printSpotCell(SPOTS_PER_FLOOR + r, nextSpot);
  Serial.print('|'); printCentered("v", MAP_LANE_W); Serial.print('|');
  Serial.print("  |"); printCentered(r == 0 ? "U-turn" : (r == 3 ? "RAMP" : "^   v"), MAP_RAMP_W); Serial.print("|  ");
  Serial.print('|'); printCentered("^", MAP_LANE_W);
  Serial.print('|'); printSpotCell(SPOTS_PER_FLOOR - 1 - r, nextSpot); Serial.print('|');
}

// קו אופקי: border = גבול עליון/תחתון של הקומות (אחרת קו חניה - הנתיב נשאר פתוח);
// rampClosed = לסגור את עמודת הרמפה (למעלה), אחרת היא ממשיכה למטה אל הכביש הראשי
void printMapLine(bool border, bool rampClosed) {
  Serial.print('+'); printRepeat('-', MAP_CELL_W); Serial.print('+');
  printRepeat(border ? '-' : ' ', MAP_LANE_W); Serial.print(border ? '+' : '|');
  Serial.print("  "); Serial.print(rampClosed ? '+' : '|');
  printRepeat(rampClosed ? '-' : ' ', MAP_RAMP_W); Serial.print(rampClosed ? '+' : '|'); Serial.print("  ");
  Serial.print(border ? '+' : '|'); printRepeat(border ? '-' : ' ', MAP_LANE_W);
  Serial.print('+'); printRepeat('-', MAP_CELL_W); Serial.print('+');
}

// שורה אחת עם הרכבים שמחכים (בציור הגרפי זה הצד של העמוד)
void printWaitingLine() {
  const int SHOWN = 5;
  int ids[SHOWN];
  int n = collectWaitingCars(ids, SHOWN);
  Serial.print("  WAITING: ");
  if (n == 0) { Serial.println("none"); return; }
  for (int k=0; k<n && k<SHOWN; k++) {
    int spot = whereIsCarReserved(ids[k]);
    if (k) Serial.print(" | ");
    Serial.print('#'); Serial.print(ids[k]); Serial.print(" -> ");
    if (spot >= 0) {
      Serial.print("spot "); Serial.print(spot);
      Serial.print(" ("); Serial.print(reservationSecondsLeft(spot)); Serial.print("s)");
    } else {
      Serial.print("no spot");
    }
  }
  if (n > SHOWN) { Serial.print(" | +"); Serial.print(n - SHOWN); Serial.print(" more"); }
  Serial.println();
}

// ציור הטקסט (גיבוי לתמונה הגרפית), באותו סידור כמו הדגם, ומתחתיו הרכבים שמחכים
void printLotMap(bool withLegend) {
  int nextSpot = findAvailableSpot(NULL);

  Serial.println();
  printCentered("FLOOR 1 - deck", MAP_SIDE_W); Serial.print("  ");
  printCentered("ramp", MAP_RAMP_W + 2); Serial.print("  ");
  printCentered("FLOOR 0 - ground", MAP_SIDE_W); Serial.println();
  printMapLine(true, true); Serial.println();
  for (int r=0; r<SPOTS_PER_FLOOR; r++) {
    printMapRow(r, nextSpot); Serial.println();
    printMapLine(r == SPOTS_PER_FLOOR-1, false); Serial.println();
  }
  // הכביש הראשי והכניסה, מתחת לרמפה
  printRepeat(' ', MAP_SIDE_W + 2); Serial.print('|'); printCentered("^   v", MAP_RAMP_W); Serial.print('|');
  printRepeat(' ', MAP_SIDE_W + 2); Serial.println();
  printRepeat(' ', MAP_SIDE_W + 2); Serial.print('|'); printCentered("ENTRANCE", MAP_RAMP_W); Serial.print('|');
  printRepeat(' ', MAP_SIDE_W + 2); Serial.println();
  printWaitingLine();
  if (withLegend) {
    Serial.println("  [#n OK] parked right | [#n WRONG] wrong spot | RES #n 45s = reserved");
    Serial.println("  >> NEXT << next car  | //BLOCKED// object on sensor | * = physical spot");
  }
}

// מצב החניון כ-JSON, לעמוד האינטרנט. העמוד מצייר את הדגם ומציב עליו את הנתונים האלה:
//   geo  = המידות שנמדדו (ס"מ): קטעי הדרך, מידות חניה ורכב - העמוד מציג אותן ומשרטט לפיהן
//   sp   = כל החניות (קומה, מיקום בשורה של הקומה, מצב, רכב, נכון/לא, שניות לשריון, מרחק)
//   wait = הרכבים שמחכים (מזהה, חניה מיועדת, שניות לשריון) - הצד של התמונה
//   seq  = מונה פקודות status | next = החניה המומלצת לרכב הבא
String buildStateJson() {
  const int MAX_WAIT_LISTED = 16;   // מספר הרכבים המקסימלי שנשלח לצד; waitN הוא הסכום המלא
  float nextDist = 0;
  int nextSpot = findAvailableSpot(&nextDist);
  int waitIds[MAX_WAIT_LISTED];
  int waitN = collectWaitingCars(waitIds, MAX_WAIT_LISTED);
  String j;
  j.reserve(2400);
  j += "{\"up\":";    j += (unsigned long)(millis() / 1000);
  j += ",\"seq\":";   j += statusSeq;
  j += ",\"ttl\":";   j += (unsigned long)(RESERVATION_TIMEOUT_MS / 1000);
  j += ",\"floors\":"; j += FLOORS;
  j += ",\"per\":";    j += SPOTS_PER_FLOOR;
  j += ",\"unit\":\""; j += DIST_UNIT; j += '"';
  j += ",\"geo\":{\"e0\":"; j += String(ENT_TO_FLOOR0_CM, 1);
  j += ",\"er\":";     j += String(ENT_TO_RAMP_CM, 1);
  j += ",\"ramp\":";   j += String(RAMP_CM, 1);
  j += ",\"f1\":";     j += String(FLOOR1_TO_FIRST_CM, 1);
  j += ",\"sw\":";     j += String(SPOT_WIDTH_CM, 1);
  j += ",\"sl\":";     j += String(SPOT_LENGTH_CM, 1);
  j += ",\"car\":[";   j += String(CAR_LENGTH_CM, 1); j += ','; j += String(CAR_WIDTH_CM, 1);
  j += ',';            j += String(CAR_HEIGHT_CM, 1);
  j += "]},\"real\":"; j += REAL_SPOT_INDEX;
  j += ",\"next\":";   j += nextSpot;
  j += ",\"nextd\":";  j += String(nextSpot >= 0 ? nextDist : 0.0f, 1);
  j += ",\"sp\":[";
  for (int i=0;i<TOTAL_SPOTS;i++) {
    int st  = spotState(i);
    int car = (st == SP_OCCUPIED) ? parkedCarId[i] : (st == SP_RESERVED ? assignedCarId[i] : -1);
    if (i) j += ',';
    j += "{\"f\":";   j += (int)SPOT_FL[i];
    j += ",\"k\":";   j += spotIndexOnFloor(i);
    j += ",\"s\":";   j += st;
    j += ",\"car\":"; j += car;
    j += ",\"ok\":";  j += (wasCorrect[i] ? 1 : 0);
    j += ",\"left\":"; j += (st == SP_RESERVED ? (unsigned long)reservationSecondsLeft(i) : 0UL);
    j += ",\"d\":";   j += String(spotDist[i], 1);
    j += "}";
  }
  j += "],\"waitN\":"; j += waitN;
  j += ",\"wait\":[";
  for (int k=0; k<waitN && k<MAX_WAIT_LISTED; k++) {
    int spot = whereIsCarReserved(waitIds[k]);   // -1 = השריון שלו פג
    if (k) j += ',';
    j += "{\"car\":";  j += waitIds[k];
    j += ",\"spot\":"; j += spot;
    j += ",\"left\":"; j += (spot >= 0 ? (unsigned long)reservationSecondsLeft(spot) : 0UL);
    j += "}";
  }
  j += "]}";
  return j;
}

// ==================== [5] פעולות החניון ====================
int registerCar() {
  if (nextCarId >= MAX_CARS) {
    Serial.println("[ERROR]  car id limit reached - restart the board");
    return -1;
  }
  carExited[nextCarId] = false;
  return nextCarId++;
}

int reserveSpotFor(int carId, float *outDist) {
  int spot = findAvailableSpot(outDist);
  if (spot >= 0) { assignedCarId[spot] = carId; reservedAtMs[spot] = millis(); }
  return spot;
}

void printSpotAndDist(int spot, float d) {
  Serial.print(" spot "); Serial.print(spot);
  Serial.print(" (floor "); Serial.print(SPOT_FL[spot]); Serial.print(")");
  Serial.print("  dist "); Serial.print(d,1); Serial.print(' '); Serial.print(DIST_UNIT);
}

// ההמלצה לרכב הבא (לפי בקשת המנחה) - מודפסת אחרי כל שינוי במצב החניון
void printNextRecommendation() {
  float d; int spot = findAvailableSpot(&d);
  if (spot == -1) { Serial.println("[NEXT]   lot full - next car will be turned away"); return; }
  Serial.print("[NEXT]   next car ->"); printSpotAndDist(spot, d);
  Serial.println();
}

// אחרי כל שינוי במצב החניון: ההמלצה לרכב הבא. התמונה הגרפית מתעדכנת מעצמה (העמוד שואל את
// ה-ESP32 כל שנייה); רק כשאין עמוד גרפי מודפס כאן גם ציור הטקסט
void printLotUpdate() {
  if (showTextMap()) printLotMap(false);
  printNextRecommendation();
}

int assignNewCar() {
  if (findAvailableSpot(NULL) == -1) {
    Serial.println("[ENTRY]  LOT FULL - no car created");
    return -1;
  }
  int id = registerCar();
  if (id < 0) return -1;
  float d; int spot = reserveSpotFor(id, &d);

  Serial.print("[ENTRY]  car #"); Serial.print(id);
  Serial.print("  -> go to"); printSpotAndDist(spot, d);
  if (spot == REAL_SPOT_INDEX) Serial.print("  [PHYSICAL]");
  Serial.println();
  printLotUpdate();
  return spot;
}

// הרכב שהחניה שלו נתפסה מקבל חניה חדשה. אם אין אף חניה פנויה - הוא יוצא מהחניון
void rerouteCar(int carId, int takenSpot) {
  float d; int spot = reserveSpotFor(carId, &d);
  Serial.print("[REROUTE] car #"); Serial.print(carId);
  Serial.print(": spot "); Serial.print(takenSpot); Serial.print(" was taken");
  if (spot < 0) {
    Serial.println(" and the lot is full");
    Serial.print("[LEAVE]  car #"); Serial.print(carId);
    Serial.println(" must leave - exited the lot without parking");
    carExited[carId] = true;
    return;
  }
  Serial.print(", go to"); printSpotAndDist(spot, d);
  Serial.println();
}

void parkCarAt(int spot, int carId) {
  if (spot < 0 || spot >= TOTAL_SPOTS) {
    Serial.println("[ERROR]  invalid spot number"); return;
  }
  if (carId < 0) {
    Serial.println("[ERROR]  invalid car id"); return;
  }
  if (!isCarInLot(carId)) {
    Serial.print("[ERROR]  car #"); Serial.print(carId);
    Serial.println(" is not in the lot"); return;
  }
  int already = whereIsCarParked(carId);
  if (already >= 0) {
    Serial.print("[ERROR]  car #"); Serial.print(carId);
    Serial.print(" is already parked at spot "); Serial.println(already);
    return;
  }
  if (occupied[spot]) {
    Serial.print("[ERROR]  spot "); Serial.print(spot);
    Serial.print(" already taken by car #"); Serial.println(parkedCarId[spot]);
    return;
  }

  // חניה ששוריינה לרכב אחר: פיזית אי אפשר למנוע את החניה, לכן מקבלים אותה
  // ומפנים את הרכב השני לחניה אחרת
  int displaced = (assignedCarId[spot] >= 0 && assignedCarId[spot] != carId) ? assignedCarId[spot] : -1;
  int ownSpot   = whereIsCarReserved(carId);
  bool correct  = (ownSpot == spot);

  clearReservationFor(carId);      // הרכב זוהה בחניה - השריון המקורי שלו משתחרר מיד
  assignedCarId[spot]=-1;
  occupied[spot]=true; parkedCarId[spot]=carId; wasCorrect[spot]=correct;

  Serial.print("[PARK]   car #"); Serial.print(carId);
  Serial.print("  @ spot "); Serial.print(spot);
  Serial.print("  dist "); Serial.print(spotDist[spot],1); Serial.print(' ');
  Serial.print(DIST_UNIT); Serial.print("  -> ");
  if (correct) Serial.println("CORRECT");
  else {
    Serial.print("WRONG SPOT!");
    if (ownSpot >= 0) { Serial.print("  (assigned spot "); Serial.print(ownSpot); Serial.print(" released)"); }
    Serial.println();
  }

  if (displaced >= 0) rerouteCar(displaced, spot);
  printLotUpdate();
}

// הרכב יוצא מהחניון כולו - בין אם חנה ובין אם עדיין היה בדרך לחניה
void leaveCar(int carId, bool fromSensor) {
  if (carId < 0) {
    Serial.println("[ERROR]  invalid car id"); return;
  }
  if (!isCarInLot(carId)) {
    Serial.print("[ERROR]  car #"); Serial.print(carId);
    Serial.println(" is not in the lot"); return;
  }
  int spot = whereIsCarParked(carId);
  Serial.print("[LEAVE]  car #"); Serial.print(carId);
  if (spot >= 0) {
    occupied[spot]=false; parkedCarId[spot]=-1;
    Serial.print("  left spot "); Serial.print(spot);
    if (fromSensor) Serial.print(" (sensor)");
    Serial.println(" - exited the lot");
    if (spot == REAL_SPOT_INDEX && !fromSensor && spotSensedStable)
      Serial.println("[NOTE]   the sensor still sees a car at spot 0 - BLOCKED until it clears");
  } else {
    clearReservationFor(carId);
    Serial.println("  exited the lot without parking");
  }
  carExited[carId] = true;
  printLotUpdate();
}

// שריון שלא מומש בזמן משתחרר. הרכב נשאר בחניון ויכול עדיין לחנות (ייחשב WRONG)
void checkReservationTimeouts() {
  unsigned long now = millis();
  for (int i=0;i<TOTAL_SPOTS;i++) {
    if (assignedCarId[i] < 0) continue;
    if (now - reservedAtMs[i] < RESERVATION_TIMEOUT_MS) continue;
    Serial.print("[TIMEOUT] car #"); Serial.print(assignedCarId[i]);
    Serial.print(" did not reach spot "); Serial.print(i);
    Serial.print(" within "); Serial.print(RESERVATION_TIMEOUT_MS/1000);
    Serial.println(" s - spot released");
    assignedCarId[i] = -1;
    printLotUpdate();
  }
}

// רכב הגיע פיזית לחניה 0. החיישן לא יודע מי הרכב, לכן מניחים:
// הרכב שהחניה שוריינה לו -> אחרת הרכב האחרון שעדיין בדרך לחניה.
// רכבים נכנסים לחניון רק דרך הכניסה. אם אף רכב לא בדרך, לא ידוע של מי העצם -
// החניה חסומה (לא שולחים אליה רכבים) עד שהיא מתפנה
void onSpotSensorArrive(float cm) {
  Serial.print("[SENSOR] spot 0: object detected at "); Serial.print(cm,1); Serial.println(" cm");
  if (occupied[REAL_SPOT_INDEX]) return;   // כבר נרשם שם רכב דרך ה-Serial
  int carId = assignedCarId[REAL_SPOT_INDEX];
  if (carId < 0) carId = newestUnparkedCar();
  if (carId < 0) {
    Serial.println("[WARN]   no car is on its way - spot 0 is BLOCKED until it clears");
    printLotUpdate();
    return;
  }
  parkCarAt(REAL_SPOT_INDEX, carId);
}

// הרכב עזב פיזית את חניה 0 = יצא מהחניון. אם החניה הייתה חסומה - היא שוב זמינה
bool spotLeavePending = false;      // הרכב שיצא מחניה 0 עדיין לא נראה בנתיב היציאה
unsigned long spotLeaveAtMs = 0;

void onSpotSensorLeave(float cm) {
  Serial.print("[SENSOR] spot 0: clear at "); Serial.print(cm,1); Serial.println(" cm");
  if (occupied[REAL_SPOT_INDEX]) {
    spotLeavePending = true; spotLeaveAtMs = millis();
    leaveCar(parkedCarId[REAL_SPOT_INDEX], true);
  }
  else printLotUpdate();   // הייתה חסומה, או שהרכב כבר שוחרר דרך ה-Serial
}

// חיישן היציאה לא יודע איזה רכב עבר, אז בוחרים: הרכב שחונה בחניה 0 (היחיד שיש לו חיישן),
// אחרת הרכב החונה עם המזהה הנמוך ביותר, אחרת כל רכב שעדיין בחניון. אפשר לשנות כאן את הכלל
int pickCarForExit() {
  if (occupied[REAL_SPOT_INDEX]) return parkedCarId[REAL_SPOT_INDEX];
  for (int id=0; id<nextCarId; id++) if (!carExited[id] && whereIsCarParked(id) >= 0) return id;
  for (int id=0; id<nextCarId; id++) if (!carExited[id]) return id;
  return -1;
}

// נראה רכב בנתיב היציאה
void onGateExit(float cm) {
  Serial.print("[SENSOR] exit lane: car detected at "); Serial.print(cm,1); Serial.println(" cm");
  if (spotLeavePending && millis() - spotLeaveAtMs < EXIT_MATCH_MS) {
    spotLeavePending = false;   // זה הרכב ש-onSpotSensorLeave כבר הוציא - לא מוציאים עוד רכב
    Serial.println("[NOTE]   this is the car that just left spot 0 - already removed");
    return;
  }
  spotLeavePending = false;
  int id = pickCarForExit();
  if (id < 0) { Serial.println("[WARN]   exit lane saw a car but no car is in the lot"); return; }
  Serial.print("[NOTE]   the exit sensor cannot tell which car it is - removing car #"); Serial.println(id);
  leaveCar(id, false);
}

// status = "תרענן את התמונה". ב-Serial נשארת רק שורת סיכום; הטבלה של v8 הוחלפה בתמונה הגרפית
// (מי חונה, נכון/לא נכון, רכבים שמחכים ושניות לשריון - הכול בעמוד). הציור עצמו קורה בדפדפן
void printStatus() {
  statusSeq++;   // העמוד רואה את המספר החדש ומרענן מיד (parking_page.h)
  int count[4] = {0, 0, 0, 0};   // לפי SP_FREE / SP_RESERVED / SP_OCCUPIED / SP_BLOCKED
  for (int i=0;i<TOTAL_SPOTS;i++) count[spotState(i)]++;
  Serial.print("[STATUS] "); Serial.print(count[SP_OCCUPIED]);
  Serial.print(" occupied, "); Serial.print(count[SP_RESERVED]);
  Serial.print(" reserved, ");
  if (count[SP_BLOCKED]) Serial.print("1 blocked, ");
  Serial.print(count[SP_FREE]);
  Serial.print(" free  (of "); Serial.print(TOTAL_SPOTS); Serial.println(")");

#if ENABLE_WEB_VIEW
  if (webRunning) {
    Serial.print("[VISUAL] picture refreshed: http://"); Serial.println(WiFi.softAPIP());
  }
#endif
  if (showTextMap()) printLotMap(true);
  printNextRecommendation();
}

void printHelp() {
  Serial.println("[HELP]   new | park <spot> <id> | leave <id> (alias: cancel) | status | check [sec]");
}

// ==================== [6] מנתח פקודות ====================
unsigned long checkUntilMs = 0;

// מפרק שורה למילים. מחזיר כמה מילים יש בשורה (גם יותר מ-4) ושומר את 4 הראשונות.
// מילה ארוכה מ-15 תווים נשמרת ריקה, כדי שלא תיחתך בשקט למילה אחרת
int splitTokens(const char* line, char tok[4][16]) {
  int n=0, i=0;
  while (line[i]) {
    while (line[i]==' '||line[i]=='\t') i++;
    if (!line[i]) break;
    int j=0;
    while (line[i] && line[i]!=' ' && line[i]!='\t') {
      if (n<4 && j<15) tok[n][j]=line[i];
      j++; i++;
    }
    if (n<4) tok[n][j<=15 ? j : 0]='\0';
    n++;
  }
  return n;
}

// ממיר טוקן למספר שלם; false אם הוא לא מספר (atoi היה הופך "abc" ל-0 = רכב 0)
bool parseNumber(const char* s, int* out) {
  if (!*s) return false;
  char* end;
  long v = strtol(s, &end, 10);
  if (*end || v < -99999 || v > 99999) return false;
  *out = (int)v;
  return true;
}

// בודק שלפקודה יש בדיוק את מספר הארגומנטים הנכון; אחרת מדפיס איך להשתמש בה
bool argsOk(int n, int minArgs, int maxArgs, const char* usage) {
  if (n-1 >= minArgs && n-1 <= maxArgs) return true;
  Serial.print("[ERROR]  usage: "); Serial.println(usage);
  return false;
}

void handleCommand(const char* line) {
  char tok[4][16];
  for (int i=0;i<4;i++) tok[i][0]='\0';
  int n = splitTokens(line, tok);
  if (n == 0) return;

  if (strcasecmp(tok[0],"new")==0) {
    if (!argsOk(n, 0, 0, "new")) return;
    assignNewCar();
  } else if (strcasecmp(tok[0],"park")==0) {
    int spot, id;
    if (!argsOk(n, 2, 2, "park <spot> <id>")) return;
    if (!parseNumber(tok[1],&spot) || !parseNumber(tok[2],&id)) {
      Serial.println("[ERROR]  usage: park <spot> <id>"); return;
    }
    parkCarAt(spot, id);
  } else if (strcasecmp(tok[0],"leave")==0 || strcasecmp(tok[0],"cancel")==0) {
    int id;
    if (!argsOk(n, 1, 1, "leave <id>")) return;
    if (!parseNumber(tok[1],&id)) { Serial.println("[ERROR]  usage: leave <id>"); return; }
    leaveCar(id, false);
  } else if (strcasecmp(tok[0],"status")==0) {
    if (!argsOk(n, 0, 0, "status")) return;
    printStatus();
  } else if (strcasecmp(tok[0],"check")==0) {
    int sec = CHECK_DEFAULT_S;
    if (!argsOk(n, 0, 1, "check [seconds 1-60]")) return;
    if (n == 2 && (!parseNumber(tok[1],&sec) || sec < 1 || sec > 60)) {
      Serial.println("[ERROR]  usage: check [seconds 1-60]"); return;
    }
    checkUntilMs = millis() + (unsigned long)sec * 1000;
    Serial.print("[CHECK]  sensor readings for "); Serial.print(sec); Serial.println(" seconds...");
  } else if (strcasecmp(tok[0],"help")==0) {
    printHelp();
  } else {
    Serial.print("[ERROR]  unknown command: "); Serial.println(tok[0][0] ? tok[0] : "(too long)");
    printHelp();
  }
}

// ==================== [7] חיישנים ====================
float readDistanceCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(5);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return -1;
  return (duration / 2.0) / 29.1;
}

// קריאה תקינה = חזר הד והמרחק לפחות 2 ס"מ. פולס קצר יותר הוא רעש חשמלי על קו ה-Echo
// (נמדד 0.0-0.1 ס"מ), לא עצם אמיתי. ב-check עדיין מודפס הערך הגולמי, לצורך אבחון
bool validReading(float cm) { return cm >= MIN_VALID_CM; }

// מצב השער: gateLatched = רכב כבר נספר ועדיין עומד מול החיישן (נספר פעם אחת בלבד)
const int GATE_EMPTY = 0, GATE_ENTRY = 1, GATE_EXIT = 2;

// לאיזה מצב שייכת קריאה תקינה של חיישן הכניסה: מעל GATE_CAR_BELOW_CM / אין הד = ריק;
// מתחת לו = רכב, ואם מוגדר נתיב יציאה - הנתיב שהערך שלו (ENTRY / EXIT) קרוב יותר לקריאה
int classifyGate(float cm) {
  if (cm < 0 || cm >= GATE_CAR_BELOW_CM) return GATE_EMPTY;
  if (GATE_EXIT_CM > 0 && fabs(cm - GATE_EXIT_CM) < fabs(cm - GATE_ENTRY_CM)) return GATE_EXIT;
  return GATE_ENTRY;
}

// להדפסה בהפעלה
void printGateBands() {
  Serial.print("[BOOT]   entrance sensor: empty ~"); Serial.print(GATE_EMPTY_CM,1);
  Serial.print(" cm, car ~"); Serial.print(GATE_ENTRY_CM,1);
  if (GATE_EXIT_CM > 0) { Serial.print(" cm, exit lane ~"); Serial.print(GATE_EXIT_CM,1); }
  Serial.print(" cm -> a car is counted below "); Serial.print(GATE_CAR_BELOW_CM, 1);
  Serial.println(GATE_EXIT_CM > 0 ? " cm (entry or exit: the nearer of the two)" : " cm");
}
bool gateLatched    = false;
int  gateCand       = GATE_EMPTY;   // הנתיב שנראה בדגימות האחרונות
int  gateCandCount  = 0;            // כמה דגימות רצופות בו
int  gateClearCount = 0;            // כמה דגימות "ריק" רצופות
int  spotDisagreeCount = 0;   // דגימות רצופות שסותרות את המצב היציב של החניה
bool firstSample       = true;

// ==================== [7ב] עמוד אינטרנט עם תמונת החניון ====================
// הדפדפן מבקש "/" פעם אחת (העמוד מ-parking_page.h) ואז "/state" כל שנייה (JSON מ-buildStateJson).
// *** הציור הגרפי רץ בדפדפן (JavaScript בתוך parking_page.h), לא כאן: ה-ESP32 רק מגיש נתונים. ***
// הכול נשאר ב-ESP32 והדפדפן: אין צורך באינטרנט או בשרת חיצוני
#if ENABLE_WEB_VIEW
WebServer webServer(80);

void handleWebRoot()  { webServer.send_P(200, "text/html", PARKING_PAGE); }
void handleWebState() {
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", buildStateJson());
}

void startWebView() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(WEB_SSID)) {
    Serial.println("[WEB]    WiFi failed to start - picture page is off (the Serial Monitor still works)");
    return;
  }
  webServer.on("/", handleWebRoot);
  webServer.on("/state", handleWebState);
  webServer.begin();
  webRunning = true;
  Serial.print("[WEB]    picture page: join WiFi \""); Serial.print(WEB_SSID);
  Serial.print("\" then open http://"); Serial.println(WiFi.softAPIP());
}
#endif

// ==================== [8] setup / loop ====================
unsigned long lastSampleTime = 0, lastBlinkTime = 0;
bool blinkState = false;
char lineBuf[48];
int  lineLen = 0;

void setup() {
  Serial.begin(9600);
  pinMode(trigPin1, OUTPUT); pinMode(echoPin1, INPUT);
  pinMode(trigPin2, OUTPUT); pinMode(echoPin2, INPUT);
  pinMode(ledPin, OUTPUT);

  for (int i=0;i<TOTAL_SPOTS;i++) {
    occupied[i]=false; assignedCarId[i]=-1; parkedCarId[i]=-1; wasCorrect[i]=true;
    reservedAtMs[i]=0;
  }
  computeSpotDistances();

  Serial.println();
  Serial.print("[BOOT]   ready: "); Serial.print(TOTAL_SPOTS);
  Serial.println(" spots, 2 floors");
  Serial.print("[BOOT]   nearest spot "); Serial.print(spotDist[0],1);
  Serial.print(' '); Serial.print(DIST_UNIT); Serial.print(" | farthest ");
  Serial.print(spotDist[TOTAL_SPOTS-1],1); Serial.print(' '); Serial.println(DIST_UNIT);
  Serial.print("[BOOT]   reservations expire after ");
  Serial.print(RESERVATION_TIMEOUT_MS/1000); Serial.println(" s");
  printGateBands();
  Serial.print("[BOOT]   spot 0 sensor: car arrives below "); Serial.print(SPOT_ARRIVE_CM,1);
  Serial.print(" cm, leaves above "); Serial.print(SPOT_LEAVE_CM,1); Serial.println(" cm");
#if ENABLE_WEB_VIEW
  startWebView();
#endif
  printHelp();
  if (showTextMap()) printLotMap(true);   // החניון הריק; בלי עמוד גרפי זו התמונה כבר מההפעלה
  printNextRecommendation();
}

void loop() {
  unsigned long currentMillis = millis();

#if ENABLE_WEB_VIEW
  if (webRunning) webServer.handleClient();
#endif

  // --- 1. פקודות מה-Serial ---
  while (Serial.available()) {
    char c = Serial.read();
    if (c=='\n') { lineBuf[lineLen]='\0'; handleCommand(lineBuf); lineLen=0; }
    else if (c!='\r' && lineLen < (int)sizeof(lineBuf)-1) lineBuf[lineLen++]=c;
  }

  // --- 2. שריונים שפג תוקפם ---
  checkReservationTimeouts();

  // --- 3. דגימת חיישנים ---
  if (currentMillis - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = currentMillis;

    float distEntrance = readDistanceCm(trigPin1, echoPin1);
    delay(CROSSTALK_DELAY_MS);
    float distSpot     = readDistanceCm(trigPin2, echoPin2);

    // סיווג קריאת השער: אין הד = כביש פתוח (ריק); פולס קצר מ-2 ס"מ = רעש חשמלי, מתעלמים ממנו
    bool gateNoise = (distEntrance >= 0 && !validReading(distEntrance));
    int  gateBand  = gateNoise ? GATE_EMPTY : classifyGate(distEntrance);
    bool spotValid     = validReading(distSpot);   // הרצפה תמיד בטווח, אז קריאה לא תקינה היא תקלה
    // היסטרזיס: כשהחניה ריקה צריך לרדת מתחת ל-ARRIVE, כשהיא תפוסה צריך לעלות מעל LEAVE
    bool spotSensed    = spotValid && distSpot < (spotSensedStable ? SPOT_LEAVE_CM : SPOT_ARRIVE_CM);

    if (firstSample) {
      gateLatched       = (!gateNoise && gateBand != GATE_EMPTY);   // רכב שכבר עומד בהפעלה לא נספר
      spotSensedStable  = spotValid && distSpot < SPOT_ARRIVE_CM;
      firstSample = false;
    } else {
      // השער: רכב נספר אחרי GATE_CONFIRM_SAMPLES דגימות רצופות באותו נתיב, ופעם אחת בלבד -
      // עד שהכביש נשאר ריק GATE_CLEAR_SAMPLES דגימות. נתיב כניסה = רכב חדש; נתיב יציאה = רכב יוצא
      if (!gateNoise) {
        if (gateBand == GATE_EMPTY) {
          gateCand = GATE_EMPTY; gateCandCount = 0;
          if (gateLatched && ++gateClearCount >= GATE_CLEAR_SAMPLES) { gateLatched = false; gateClearCount = 0; }
        } else {
          gateClearCount = 0;
          if (!gateLatched) {
            if (gateBand == gateCand) gateCandCount++; else { gateCand = gateBand; gateCandCount = 1; }
            if (gateCandCount >= GATE_CONFIRM_SAMPLES) {
              gateLatched = true; gateCandCount = 0;
              if (gateBand == GATE_ENTRY) {
                Serial.print("[SENSOR] entrance: car detected at "); Serial.print(distEntrance,1); Serial.println(" cm");
                assignNewCar();
              } else onGateExit(distEntrance);
            }
          }
        }
      }

      // חניה 0: שינוי מצב דורש SPOT_CONFIRM_SAMPLES דגימות תקינות רצופות, כדי שרעש
      // לא ייצור חניה או יציאה מדומה. דגימה לא תקינה מאפסת את הרצף - הרעש מגיע
      // בפרצים, ובזמן פרץ לא סומכים על החיישן
      if (!spotValid || spotSensed == spotSensedStable) {
        spotDisagreeCount = 0;
      } else if (++spotDisagreeCount >= SPOT_CONFIRM_SAMPLES) {
        spotSensedStable  = spotSensed;
        spotDisagreeCount = 0;
        if (spotSensed) onSpotSensorArrive(distSpot);
        else            onSpotSensorLeave(distSpot);
      }
    }

    if (currentMillis < checkUntilMs) {
      Serial.print("[CHECK]  entrance ");
      if (distEntrance < 0) Serial.print("  --  ");
      else { Serial.print(distEntrance,1); Serial.print(" cm"); }
      Serial.print(" | spot ");
      if (distSpot < 0) Serial.print("  --  ");
      else { Serial.print(distSpot,1); Serial.print(" cm"); }
      Serial.print(" | spot 0 ");
      if (occupied[REAL_SPOT_INDEX])              Serial.print("OCCUPIED");
      else if (assignedCarId[REAL_SPOT_INDEX]>=0) Serial.print("RESERVED");
      else if (spot0Blocked())                    Serial.print("BLOCKED");
      else                                        Serial.print("free");
      Serial.print(" | gate ");
      if (gateNoise)                    Serial.println("noise");
      else if (gateBand == GATE_EXIT)   Serial.println("EXIT lane");
      else if (gateBand == GATE_ENTRY)  Serial.println("CAR (entry)");
      else                              Serial.println("empty");
    }
  }

  // --- 4. LED של החניה הפיזית ---
  // כבוי = אין רכב חונה (גם כשמשוריינת) | דולק = חנה נכון | מהבהב = חנה לא נכון
  if (!occupied[REAL_SPOT_INDEX]) {
    digitalWrite(ledPin, LOW);
  } else if (wasCorrect[REAL_SPOT_INDEX]) {
    digitalWrite(ledPin, HIGH);
  } else if (currentMillis - lastBlinkTime >= BLINK_INTERVAL_MS) {
    blinkState = !blinkState;
    digitalWrite(ledPin, blinkState ? HIGH : LOW);
    lastBlinkTime = currentMillis;
  }
}
