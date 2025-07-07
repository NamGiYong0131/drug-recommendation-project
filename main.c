#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h> 
#include <windows.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define CSV_FILE      "drugs.csv"
#define MAX_LINE      256
#define MAX_DRUGS     100
#define MAX_STACK     50
#define MAX_PAIN_TYPES 50
#define HASH_SIZE     211  // 소수로 해시 충돌 최소화

void   listDrugs();
void   addDrug();
void   deleteDrug();
void   modifyDrug();
bool   saveDrugsToCSV(const char* filename);
void   adminMenu();

// -------- 데이터 구조 --------
typedef struct {
    char name[50];
    char efficacy[100];
    int  minAge, maxAge;
    int  pregnancySafe;  // 1=임신 중 안전, 0=비안전
    int  alcoholSafe;    // 1=음주 안전, 0=비안전
    char interactions[100];
} Drug;

typedef struct HashNode {
    char        drugName[50];
    char        interaction[50];
    struct HashNode* next;
} HashNode;

typedef struct PainNode {
    char pain[50];
    struct PainNode* next;
} PainNode;

typedef struct {
    char drugName[50];
    char reason[200];
    int  drugIndex;
} DrugStack;

// -------- 전역 변수 --------
Drug*      drugs = NULL;
int        drugCount = 0;
HashNode*  hashTable[HASH_SIZE];

char       painTypes[MAX_PAIN_TYPES][50];
int        painTypeCount = 0;

char       painList[MAX_PAIN_TYPES][50];
int        painCountArr = 0;

DrugStack  resultStack[MAX_PAIN_TYPES];

// 사용자 조건
int G_age, G_isFemale, G_isPregnant, G_heavyDrinker;

// -------- 해시 함수 & 상호작용 --------
int hashFunction(const char* str) {
    unsigned int sum = 0;
    for (int i = 0; str[i]; i++) sum += (unsigned char)str[i];
    return sum % HASH_SIZE;
}

void addInteraction(const char* a, const char* b) {
    int idx = hashFunction(a);
    HashNode* node = malloc(sizeof(HashNode));
    if (!node) { perror("메모리 할당 실패"); exit(1); }
    strcpy(node->drugName, a);
    strcpy(node->interaction, b);
    node->next = hashTable[idx];
    hashTable[idx] = node;
}

// a→b 또는 b→a 방향으로도 검사할 것
bool hasInteraction(const char* a, const char* b) {
    int idx = hashFunction(a);
    for (HashNode* cur = hashTable[idx]; cur; cur = cur->next) {
        if (strcmp(cur->drugName, a)==0 && strstr(cur->interaction, b))
            return true;
    }
    return false;
}

// -------- CSV 로드 --------
bool loadDrugsFromCSV(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) { fprintf(stderr, "CSV 파일 열기 실패: %s\n", filename); return false; }
    char line[MAX_LINE];
    // 헤더 건너뛰기
    if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }

    drugs = malloc(MAX_DRUGS * sizeof(Drug));
    if (!drugs) { perror("메모리 할당 실패"); fclose(f); return false; }

    drugCount = 0;
    while (drugCount < MAX_DRUGS && fgets(line, sizeof(line), f)) {
        char* tok = strtok(line, "\t");
        if (!tok) continue;
        strncpy(drugs[drugCount].name, tok, 49);
        drugs[drugCount].name[49] = '\0';

        tok = strtok(NULL, "\t"); if (!tok) continue;
        strncpy(drugs[drugCount].efficacy, tok, 99);
        drugs[drugCount].efficacy[99] = '\0';
        drugs[drugCount].efficacy[strcspn(drugs[drugCount].efficacy, "\n")] = '\0';

        tok = strtok(NULL, "\t"); if (!tok) continue;
        drugs[drugCount].minAge = atoi(tok);
        tok = strtok(NULL, "\t"); if (!tok) continue;
        drugs[drugCount].maxAge = atoi(tok);
        tok = strtok(NULL, "\t"); if (!tok) continue;
        drugs[drugCount].pregnancySafe = atoi(tok);
        tok = strtok(NULL, "\t"); if (!tok) continue;
        drugs[drugCount].alcoholSafe = atoi(tok);
        tok = strtok(NULL, "\t"); if (!tok) continue;
        strncpy(drugs[drugCount].interactions, tok, 99);
        drugs[drugCount].interactions[99] = '\0';
        drugs[drugCount].interactions[strcspn(drugs[drugCount].interactions, "\n")] = '\0';

        drugCount++;
    }
    fclose(f);
    return true;
}

// -------- 통증 목록 관리 --------
void addPainType(const char* pain) {
    for (int i = 0; i < painTypeCount; i++)
        if (strcmp(painTypes[i], pain) == 0) return;
    if (painTypeCount < MAX_PAIN_TYPES)
        strcpy(painTypes[painTypeCount++], pain);
}

void printPainTypes() {
    const int columns = 4;
    for (int i = 0; i < painTypeCount; i++) {
        printf("| %-15s", painTypes[i]);
        if ((i+1)%columns==0 || i==painTypeCount-1) printf("\n");
    }
}

// PainNode 추가
PainNode* addPain(PainNode* head, const char* pain) {
    PainNode* node = malloc(sizeof(PainNode));
    if (!node) { perror("메모리 할당 실패"); exit(1); }
    strcpy(node->pain, pain);
    node->next = head;
    return node;
}

// -------- DFS 백트래킹 조합 탐색 --------
bool dfsAssign(int idx) {
    if (idx == painCountArr) return true;  // 모든 통증 배정 완료

    // 후보 수집
    DrugStack cands[MAX_STACK];
    int candCnt = 0;

    for (int i = 0; i < drugCount; i++) {
        // 나이·임신·음주 필터
        if (G_age < drugs[i].minAge || G_age > drugs[i].maxAge) continue;
        if (G_isFemale && G_isPregnant==0 && drugs[i].pregnancySafe==0) continue;
        if (G_heavyDrinker && drugs[i].alcoholSafe==0) continue;
        // 효능 매칭
        if (!strstr(drugs[i].efficacy, painList[idx])) continue;
        // 이전 배정 약과 상호작용 검사
        bool safe = true;
        for (int j = 0; j < idx; j++) {
            const char* prev = resultStack[j].drugName;
            const char* cur  = drugs[i].name;
            if ( hasInteraction(prev, cur) || hasInteraction(cur, prev) ) {
                safe = false; break;
            }
        }
        if (!safe) continue;
        // 후보 추가
        strcpy(cands[candCnt].drugName, drugs[i].name);
        snprintf(cands[candCnt].reason, sizeof(cands[candCnt].reason),
                 "%s에 효과적", painList[idx]);
        cands[candCnt].drugIndex = i;
        candCnt++;
    }

    if (candCnt == 0) return false;  // 이 통증 배정 실패

    // 재귀 시도
    for (int c = 0; c < candCnt; c++) {
        resultStack[idx] = cands[c];
        if (dfsAssign(idx+1)) return true;
    }
    return false;
}

// -------- 기존 방식 (통증당 첫 안전 약) --------
void recommendSimple(int age,int isFemale,int isPregnant,int heavyDrinker,
                     PainNode* pains, DrugStack* stack, int* stackSize)
{
    *stackSize = 0;
    for (PainNode* p = pains; p; p = p->next) {
        // 후보 임시
        DrugStack cands[MAX_STACK];
        int candCnt = 0;
        for (int i = 0; i < drugCount; i++) {
            if (age < drugs[i].minAge || age > drugs[i].maxAge) continue;
            if (isFemale && isPregnant==0 && drugs[i].pregnancySafe==0) continue;
            if (heavyDrinker && drugs[i].alcoholSafe==0) continue;
            if (!strstr(drugs[i].efficacy, p->pain)) continue;
            // 상호작용
            bool safe = true;
            for (int j=0; j<*stackSize; j++) {
                const char* prev = stack[j].drugName;
                const char* cur  = drugs[i].name;
                if (hasInteraction(prev, cur)||hasInteraction(cur, prev)){
                    safe=false; break;
                }
            }
            if (!safe) continue;
            // 후보
            strcpy(cands[candCnt].drugName, drugs[i].name);
            snprintf(cands[candCnt].reason, sizeof(cands[candCnt].reason),
                     "%s에 효과적", p->pain);
            cands[candCnt].drugIndex = i;
            candCnt++;
        }
        if (candCnt>0) stack[*stackSize] = cands[0];
        else {
            strcpy(stack[*stackSize].drugName, "없음");
            snprintf(stack[*stackSize].reason, sizeof(stack[*stackSize].reason),
                     "%s에 적합한 약물이 없습니다", p->pain);
        }
        (*stackSize)++;
    }
}

// -------- main --------
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (!loadDrugsFromCSV(CSV_FILE)) {
        fprintf(stderr, "CSV 로드 실패\n");
        return 1;
    }

    // 관리자 모드/사용자 모드 선택
    int mode;
    printf("모드를 선택하세요 (1: 사용자, 2: 관리자): ");
    scanf("%d", &mode);
    if (mode == 2) {
        adminMenu();
        // 관리자 종료 시 바로 프로그램 종료
        free(drugs);
        return 0;
    }

    // 해시 테이블 초기화 & 상호작용 등록(양방향)
    for (int i = 0; i < HASH_SIZE; i++) hashTable[i] = NULL;
    for (int i = 0; i < drugCount; i++) {
        char buf[100];
        strcpy(buf, drugs[i].interactions);
        char* tok = strtok(buf, ";");
        while (tok) {
            addInteraction(drugs[i].name, tok);
            addInteraction(tok, drugs[i].name);
            tok = strtok(NULL, ";");
        }
    }

    // 사용자 입력
    int painCount;
    printf("나이 (0-99): ");
    scanf("%d", &G_age);
    printf("성별 (남:0, 여:1): ");
    scanf("%d", &G_isFemale);
    if (G_isFemale) {
        printf("임신 여부 (0:아니오,1:예): ");
        scanf("%d", &G_isPregnant);
    }
    printf("하루 3잔 이상 음주? (0:아니오,1:예): ");
    scanf("%d", &G_heavyDrinker);
    printf("통증 개수 (1~%d): ", MAX_PAIN_TYPES);
    scanf("%d", &painCount);

    // 통증 목록 구축 & 출력
    for (int i=0; i<drugCount; i++) {
        char tmp[100]; strcpy(tmp, drugs[i].efficacy);
        char* tok = strtok(tmp, ";");
        while (tok) {
            addPainType(tok);
            tok = strtok(NULL, ";");
        }
    }
    printf("\n== 사용 가능한 통증 ==\n");
    printPainTypes();

    // 사용자 통증 입력
    getchar();  // 버퍼 비우기
    PainNode* pains = NULL;
    for (int i=0; i<painCount; i++) {
        char buf[50];
        printf("통증 %d: ", i+1);
        fgets(buf, sizeof(buf), stdin);
        buf[strcspn(buf,"\n")] = '\0';
        pains = addPain(pains, buf);
    }

    // pains → 배열 painList
    painCountArr = 0;
    for (PainNode* cur=pains; cur; cur=cur->next) {
        strcpy(painList[painCountArr++], cur->pain);
    }

    // DFS 시작 시간 측정
    clock_t dfsStart = clock();
    bool dfsSuccess = dfsAssign(0);

    // DFS 조합 탐색
    printf("\n== 추천 약물 (전체 조합) ==\n");
    if (dfsAssign(0)) {
        for (int i=0; i<painCountArr; i++) {
            printf("- %s: %s\n",
                   resultStack[i].drugName,
                   resultStack[i].reason);
        }
    } else {
        printf("완전 조합 실패, 단일 추천 방식으로 대체합니다.\n");
        DrugStack fallback[MAX_STACK];
        int fbSize=0;
        recommendSimple(G_age, G_isFemale, G_isPregnant, G_heavyDrinker,
                        pains, fallback, &fbSize);
        for (int i=0; i<fbSize; i++) {
            printf("- %s: %s\n",
                   fallback[i].drugName,
                   fallback[i].reason);
        }
    }
    clock_t dfsEnd   = clock();
    double dfsTime   = (double)(dfsEnd - dfsStart) / CLOCKS_PER_SEC;
    printf("DFS 실행 시간: %.3f초\n", dfsTime);
    
    // 메모리 정리
    while (pains) { PainNode* t=pains->next; free(pains); pains=t; }
    for (int i=0; i<HASH_SIZE; i++)
        while (hashTable[i]) {
            HashNode* t=hashTable[i]->next;
            free(hashTable[i]);
            hashTable[i]=t;
        }
    free(drugs);

    return 0;
}


// 약물 목록 출력
void listDrugs() {
    printf("\n== 약물 목록 ==\n");
    for (int i = 0; i < drugCount; i++) {
        printf("[%2d] %s  (효능: %s, 나이: %d~%d, 임신안전: %d, 음주안전: %d)\n",
               i,
               drugs[i].name,
               drugs[i].efficacy,
               drugs[i].minAge, drugs[i].maxAge,
               drugs[i].pregnancySafe,
               drugs[i].alcoholSafe);
    }
}

// 약물 추가
void addDrug() {
    if (drugCount >= MAX_DRUGS) {
        printf("더 이상 약물을 추가할 수 없습니다.\n");
        return;
    }
    Drug d;
    printf("약물 이름: ");          scanf(" %49[^\n]", d.name);
    printf("효능: ");             scanf(" %99[^\n]", d.efficacy);
    printf("최소 연령: ");         scanf("%d", &d.minAge);
    printf("최대 연령: ");         scanf("%d", &d.maxAge);
    printf("임신 시 안전(1/0): "); scanf("%d", &d.pregnancySafe);
    printf("음주 시 안전(1/0): "); scanf("%d", &d.alcoholSafe);
    printf("상호작용 목록(; 구분): "); scanf(" %99[^\n]", d.interactions);

    drugs[drugCount++] = d;
    printf("약물 추가 완료.\n");
}

// 약물 삭제
void deleteDrug() {
    int idx;
    listDrugs();
    printf("삭제할 약물 번호: "); scanf("%d", &idx);
    if (idx < 0 || idx >= drugCount) {
        printf("잘못된 번호입니다.\n");
        return;
    }
    for (int i = idx; i < drugCount - 1; i++) {
        drugs[i] = drugs[i + 1];
    }
    drugCount--;
    printf("약물 삭제 완료.\n");
}

// 약물 수정
void modifyDrug() {
    int idx;
    listDrugs();
    printf("수정할 약물 번호: "); scanf("%d", &idx);
    if (idx < 0 || idx >= drugCount) {
        printf("잘못된 번호입니다.\n");
        return;
    }
    Drug* d = &drugs[idx];
    printf("이름 (%s): ", d->name);                scanf(" %49[^\n]", d->name);
    printf("효능 (%s): ", d->efficacy);           scanf(" %99[^\n]", d->efficacy);
    printf("최소 연령 (%d): ", d->minAge);         scanf("%d", &d->minAge);
    printf("최대 연령 (%d): ", d->maxAge);         scanf("%d", &d->maxAge);
    printf("임신 시 안전 (%d): ", d->pregnancySafe); scanf("%d", &d->pregnancySafe);
    printf("음주 시 안전 (%d): ", d->alcoholSafe);    scanf("%d", &d->alcoholSafe);
    printf("상호작용 목록 (%s): ", d->interactions); scanf(" %99[^\n]", d->interactions);
    printf("약물 수정 완료.\n");
}

// CSV로 저장
bool saveDrugsToCSV(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) return false;
    fprintf(f, "Name\tEfficacy\tMinAge\tMaxAge\tPregnancySafe\tAlcoholSafe\tInteractions\n");
    for (int i = 0; i < drugCount; i++) {
        fprintf(f, "%s\t%s\t%d\t%d\t%d\t%d\t%s\n",
                drugs[i].name,
                drugs[i].efficacy,
                drugs[i].minAge,
                drugs[i].maxAge,
                drugs[i].pregnancySafe,
                drugs[i].alcoholSafe,
                drugs[i].interactions);
    }
    fclose(f);
    return true;
}

// 관리자 메뉴
void adminMenu() {
    int opt;
    do {
        printf("\n== 관리자 메뉴 ==\n");
        printf("1. 약물 목록\n");
        printf("2. 약물 추가\n");
        printf("3. 약물 삭제\n");
        printf("4. 약물 수정\n");
        printf("5. 저장 후 종료\n");
        printf("선택: "); scanf("%d", &opt);

        switch (opt) {
            case 1: listDrugs();            break;
            case 2: addDrug();              break;
            case 3: deleteDrug();           break;
            case 4: modifyDrug();           break;
            case 5:
                if (saveDrugsToCSV(CSV_FILE))
                    printf("CSV에 저장되었습니다.\n");
                else
                    printf("CSV 저장 실패.\n");
                break;
            default:
                printf("올바른 선택이 아닙니다.\n");
        }
    } while (opt != 5);
}