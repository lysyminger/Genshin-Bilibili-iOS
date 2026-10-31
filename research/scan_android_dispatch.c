/* Read-only metadata scanner. context-pipe is only for a non-persisting host wrapper. */
#include <stdio.h>
#ifndef GB_ANDROID_UID
#define GB_ANDROID_UID 0
#endif
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#ifdef SCAN_TEST
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

static int records;
static int private_context;
static char seen[128][2048];
static int public_number(const char *s) {
    if (!*s || strlen(s)>3) return 0;
    for (const char *p=s;*p;p++) if (!isdigit((unsigned char)*p)) return 0;
    return atoi(s)<=127;
}
static int allowed(const char *key,const char *value) {
    if (private_context && (!strcmp(key,"aid") || !strcmp(key,"time") || !strcmp(key,"dispatchSeed"))) {
        size_t size=strlen(value);if (!size || size>120) return 0;
        for (const char *p=value;*p;p++) {
            if (!strcmp(key,"dispatchSeed")) {
                if (!isalnum((unsigned char)*p) && !strchr("-_.~%+/=",*p)) return 0;
            } else if (!isdigit((unsigned char)*p)) return 0;
        }
        return 1;
    }
    if (!strcmp(key,"version")) {
        if (strncmp(value,"CNREL",5) || strlen(value)>120) return 0;
        for (const char *p=value;*p;p++) if (!isalnum((unsigned char)*p) && *p!='_' && *p!='.') return 0;
        return 1;
    }
    if (!strcmp(key,"region") || !strcmp(key,"region_name") || !strcmp(key,"server_name"))
        return !strcmp(value,"cn_gf01") || !strcmp(value,"cn_qd01");
    const char *numbers[]={"platform","channel_id","sub_channel_id","account_type","lang","binary","key_id"};
    for (unsigned i=0;i<sizeof(numbers)/sizeof(numbers[0]);i++) if (!strcmp(key,numbers[i])) return public_number(value);
    return 0;
}
static int metadata(const char *input,char *output,size_t capacity) {
    const char *s=NULL;const char *kind="full_uri";
    if (!strncmp(input,"https://",8)) s=input+8;
    else if (!strncmp(input,"http://",7)) s=input+7;
    else if (!strncmp(input,"/query_",7)) {s=input;kind="relative_target_candidate";}
    else if (!strncmp(input,"version=CNREL",13)) {s=input;kind="query_fragment_candidate";}
    else if (private_context && !strncmp(input,"dispatchSeed=",13)) {s=input;kind="private_field_fragment";}
    else return 0;
    const char *hosts[]={"dispatchcnglobal.yuanshen.com","cngfdispatch.yuanshen.com","cnqddispatch.yuanshen.com"};
    const char *host=NULL;
    if (!strcmp(kind,"full_uri")) {
        for (int i=0;i<3;i++) if (!strncmp(s,hosts[i],strlen(hosts[i])) && s[strlen(hosts[i])]=='/') {host=hosts[i];break;}
        if (!host) return 0;
        s+=strlen(host);
    } else host="not_in_fragment";
    const char *paths[]={"/query_region_list","/query_cur_region"};const char *path=NULL;
    if (!strcmp(kind,"query_fragment_candidate") || !strcmp(kind,"private_field_fragment")) path="not_in_fragment";
    else for (int i=0;i<2;i++) if (!strncmp(s,paths[i],strlen(paths[i])) &&
        (!s[strlen(paths[i])] || s[strlen(paths[i])]=='?')) {path=paths[i];break;}
    if (!path) return 0;
    int n=snprintf(output,capacity,"{\"kind\":\"%s\",\"host\":\"%s\",\"path\":\"%s\",\"params\":{",kind,host,path);
    if (n<0 || (size_t)n>=capacity) return 0;
    size_t pos=(size_t)n;int fields=0,names_count=0;char names[24][32];
    if (strcmp(kind,"query_fragment_candidate") && strcmp(kind,"private_field_fragment")) s+=strlen(path);
    if (*s=='?') s++;
    while (*s && fields<16) {
        const char *end=strchr(s,'&');if (!end) end=s+strlen(s);
        const char *eq=memchr(s,'=',(size_t)(end-s));
        if (eq && eq-s<32 && end-eq-1<128) {
            char key[32],value[128];memcpy(key,s,(size_t)(eq-s));key[eq-s]=0;
            memcpy(value,eq+1,(size_t)(end-eq-1));value[end-eq-1]=0;
            int safe_key=*key && (isalpha((unsigned char)*key) || *key=='_');
            for (char *p=key;*p;p++) if (!isalnum((unsigned char)*p) && *p!='_') safe_key=0;
            if (safe_key && names_count<24) strcpy(names[names_count++],key);
            if (allowed(key,value)) {
                n=snprintf(output+pos,capacity-pos,"%s\"%s\":\"%s\"",fields?",":"",key,value);
                if (n<0 || (size_t)n>=capacity-pos) return 0;
                pos+=(size_t)n;fields++;
            }
        }
        s=*end?end+1:end;
    }
    if (pos+3>=capacity) return 0;
    if (!strcmp(kind,"query_fragment_candidate") && fields<3) return 0;
    n=snprintf(output+pos,capacity-pos,"},\"parameter_names\":[");
    if (n<0 || (size_t)n>=capacity-pos) return 0;pos+=(size_t)n;
    for (int i=0;i<names_count;i++) {
        n=snprintf(output+pos,capacity-pos,"%s\"%s\"",i?",":"",names[i]);
        if (n<0 || (size_t)n>=capacity-pos) return 0;pos+=(size_t)n;
    }
    if (pos+3>=capacity) return 0;memcpy(output+pos,"]}\n",4);
    return 1;
}
static void inspect(const unsigned char *data,size_t size) {
    for (size_t i=0;i+16<size && records<128;i++) {
        int step=0;
        if (!memcmp(data+i,"http",4)) step=1;
        else if (i+8<size && !memcmp(data+i,"h\0t\0t\0p\0",8)) step=2;
        else if (!memcmp(data+i,"/query_",7) || !memcmp(data+i,"version=CNREL",13)) step=1;
        else if (private_context && !memcmp(data+i,"dispatchSeed=",13)) step=1;
        else if (!memcmp(data+i,"/\0q\0u\0e\0",8) || !memcmp(data+i,"v\0e\0r\0s\0",8)) step=2;
        else if (private_context && !memcmp(data+i,"d\0i\0s\0p\0",8)) step=2;
        if (!step) continue;
        char url[2048];size_t count=0;
        for (size_t p=i;p+(size_t)step<=size && count<sizeof(url)-1;p+=(size_t)step) {
            unsigned char c=data[p];
            if (step==2 && data[p+1]) break;
            if (!c || c<=32 || c>=127 || c=='"' || c=='\'') break;
            url[count++]=(char)c;
        }
        url[count]=0;char safe[2048];
        if (metadata(url,safe,sizeof(safe))) {
            int duplicate=0;
            for (int j=0;j<records;j++) if (!strcmp(seen[j],safe)) {duplicate=1;break;}
            if (!duplicate) {strcpy(seen[records],safe);fputs(safe,stdout);records++;}
        }
    }
}
#ifdef SCAN_TEST
int main(void) {
    char result[2048];
    assert(metadata("https://cnqddispatch.yuanshen.com/query_cur_region?version=CNRELAndroid7.1.0&platform=2&channel_id=14&aid=PRIVATE&combo_token=SECRET&dispatchSeed=HIDDEN",result,sizeof(result)));
    assert(strstr(result,"CNRELAndroid7.1.0") && strstr(result,"\"channel_id\":\"14\""));
    assert(!strstr(result,"PRIVATE") && !strstr(result,"SECRET") && !strstr(result,"HIDDEN") && !strstr(result,"\"aid\":"));
    assert(!metadata("https://cnqddispatch.yuanshen.com.evil.test/query_cur_region",result,sizeof(result)));
    assert(!metadata("https://cnqddispatch.yuanshen.com/login?password=SECRET",result,sizeof(result)));
    assert(!allowed("platform","123456789") && !allowed("version","CNREL7.1\nFORGED"));
    assert(metadata("/query_cur_region?version=CNRELAndroid7.1.0&platform=2&channel_id=14&aid=PRIVATE",result,sizeof(result)));
    assert(strstr(result,"relative_target_candidate") && !strstr(result,"PRIVATE"));
    assert(metadata("version=CNRELAndroid7.1.0&platform=2&channel_id=14&dispatchSeed=SECRET",result,sizeof(result)));
    assert(strstr(result,"query_fragment_candidate") && !strstr(result,"SECRET"));
    assert(!metadata("version=CNRELAndroid7.1.0",result,sizeof(result)));
    private_context=1;
    assert(metadata("/query_cur_region?version=CNRELAndroid7.1.0&platform=2&channel_id=14&aid=123&dispatchSeed=TEST_ONLY&combo_token=SECRET",result,sizeof(result)));
    assert(strstr(result,"TEST_ONLY") && strstr(result,"\"aid\":\"123\"") && !strstr(result,"SECRET"));
    assert(metadata("dispatchSeed=TEST_ONLY",result,sizeof(result)));
    assert(strstr(result,"private_field_fragment"));
    private_context=0;
    return 0;
}
#else
int main(int argc,char **argv) {
    if (argc<2 || argc>4) return 2;
    int java_only=0;
    for (int i=2;i<argc;i++) {
        if (!strcmp(argv[i],"java")) java_only=1;
        else if (!strcmp(argv[i],"context-pipe")) private_context=1;
        else return 2;
    }
    for (char *p=argv[1];*p;p++) if (!isdigit((unsigned char)*p)) return 2;
    char path[80],line[1024];unsigned uid=0;
    snprintf(path,sizeof(path),"/proc/%s/status",argv[1]);FILE *f=fopen(path,"r");if (!f) return 3;
    while (fgets(line,sizeof(line),f)) if (sscanf(line,"Uid: %u",&uid)==1) break;
    fclose(f);if (!GB_ANDROID_UID || uid!=(unsigned)GB_ANDROID_UID) {fputs("target UID mismatch\n",stderr);return 4;}
    snprintf(path,sizeof(path),"/proc/%s/cmdline",argv[1]);f=fopen(path,"r");if (!f) return 3;
    size_t got=fread(line,1,sizeof(line)-1,f);fclose(f);line[got]=0;
    const char *package="com.miHoYo.ys.bilibili";
    if (strncmp(line,package,strlen(package)) || (line[strlen(package)] && line[strlen(package)]!=':')) return 4;
    snprintf(path,sizeof(path),"/proc/%s/mem",argv[1]);int memory=open(path,O_RDONLY);
    if (memory<0) {fputs("memory read permission unavailable\n",stderr);return 5;}
    snprintf(path,sizeof(path),"/proc/%s/maps",argv[1]);f=fopen(path,"r");if (!f) {close(memory);return 3;}
    unsigned char buffer[65536+4096];uint64_t total=0;
    uint64_t total_limit=(java_only?512ULL:128ULL)*1024*1024;
    while (fgets(line,sizeof(line),f) && total<total_limit && records<128) {
        unsigned long long start,end,inode;char permissions[5];
        if (sscanf(line,"%llx-%llx %4s %*s %*s %llu",&start,&end,permissions,&inode)!=4) continue;
        if (strncmp(permissions,"rw",2) || strstr(line,"[stack]")) continue;
        if (java_only) {if (!strstr(line,"dalvik") || !strstr(line,"space")) continue;}
        else if (inode || strstr(line,"dalvik")) continue;
        size_t previous=0;uint64_t segment=0;
        uint64_t segment_limit=(java_only?384ULL:16ULL)*1024*1024;
        for (uint64_t addr=start;addr<end && segment<segment_limit && total<total_limit && records<128;) {
            size_t want=(size_t)(end-addr);if (want>65536) want=65536;
            ssize_t n=pread(memory,buffer+previous,want,(off_t)addr);
            if (n<=0) break;
            inspect(buffer,previous+(size_t)n);
            size_t hold=previous+(size_t)n;if (hold>4096) hold=4096;
            memmove(buffer,buffer+previous+(size_t)n-hold,hold);previous=hold;
            addr+=(size_t)n;segment+=(size_t)n;total+=(size_t)n;
        }
    }
    fclose(f);close(memory);
    fprintf(stderr,"scanned_bytes=%llu sanitized_records=%d\n",(unsigned long long)total,records);
    return 0;
}
#endif
