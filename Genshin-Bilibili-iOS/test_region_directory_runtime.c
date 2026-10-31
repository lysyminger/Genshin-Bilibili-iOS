#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#define __attribute__(attributes)
#define GB_ENABLE_REGION_DIRECTORY 1
#include "src/GBDiagnostic.c"
#undef __attribute__
#include "test_region_fixtures.h"

void *dlsym(void *handle, const char *name) { (void)handle; (void)name; return 0; }
enum { DATA=1, TEXT, CLASS, CONNECTION, REQUEST, URL, DELEGATE, NULL_VALUE };
struct Value { int kind; const char *text; Size size; unsigned char bytes[70000]; };
static struct Value values[32], delegate={DELEGATE}, connection={CONNECTION}, request={REQUEST}, url={URL};
static struct Value host={TEXT,"dispatchcnglobal.yuanshen.com"}, path={TEXT,"/query_region_list"}, scheme={TEXT,"https"};
static struct Value data_class={CLASS,"NSData"}, mutable_class={CLASS,"NSMutableData"}, bypass={NULL_VALUE};
static Obj association;
static int used, received, finished, failed, buffer_allocation_failed, encoded_size_mismatch;
static unsigned char delivered[150000]; static Size delivered_size;
static unsigned char decoded_gf[128], decoded_qd[128]; static Size decoded_size;
static Obj value(int kind) {
    assert(used<32); struct Value *v=&values[used++]; memset(v,0,sizeof(*v));v->kind=kind;return v;
}
static Obj data(const void *bytes, Size size) {
    struct Value *v=value(DATA);assert(size<=sizeof(v->bytes));v->size=size;
    if(bytes)memcpy(v->bytes,bytes,size);return v;
}
static Obj klass(const char *name) {
    if(!strcmp(name,"NSData"))return &data_class;
    if(!strcmp(name,"NSMutableData"))return &mutable_class;
    return 0;
}
static Sel sel(const char *name) { return (Sel)name; }
static Obj msg(Obj object, Sel command, ...) {
    if(!object)return 0;
    struct Value *v=object;const char *name=command;va_list args;va_start(args,command);Obj result=0;
    if(!strcmp(name,"respondsToSelector:"))result=(Obj)1;
    else if(!strcmp(name,"currentRequest")){assert(v==&connection);result=&request;}
    else if(!strcmp(name,"URL")){assert(v==&request);result=&url;}
    else if(!strcmp(name,"scheme")){assert(v==&url);result=&scheme;}
    else if(!strcmp(name,"host")){assert(v==&url);result=&host;}
    else if(!strcmp(name,"path")){assert(v==&url);result=&path;}
    else if(!strcmp(name,"UTF8String")){assert(v->kind==TEXT);result=(Obj)v->text;}
    else if(!strcmp(name,"data")){assert(v==&mutable_class);result=buffer_allocation_failed?0:data(0,0);}
    else if(!strcmp(name,"alloc")){assert(v==&data_class);result=value(DATA);}
    else if(!strcmp(name,"length")){result=(Obj)(__UINTPTR_TYPE__)(v->kind==TEXT?strlen(v->text):v->size);}
    else if(!strcmp(name,"setLength:")){
        Size length=va_arg(args,Size);assert(v->kind==DATA && length<=sizeof(v->bytes));
        if(length>v->size)memset(v->bytes+v->size,0,length-v->size);v->size=length;
    }
    else if(!strcmp(name,"appendData:")){
        struct Value *part=va_arg(args,Obj);assert(v->kind==DATA && part->kind==DATA && v->size+part->size<=sizeof(v->bytes));
        memcpy(v->bytes+v->size,part->bytes,part->size);v->size+=part->size;
    } else if(!strcmp(name,"initWithBase64EncodedData:options:")){
        struct Value *wire=va_arg(args,Obj);assert(va_arg(args,Size)==0);
        if(wire->size==strlen(fixture_gf_wire) && !memcmp(wire->bytes,fixture_gf_wire,wire->size))result=data(decoded_gf,decoded_size);
    } else if(!strcmp(name,"autorelease"))result=v;
    else if(!strcmp(name,"mutableCopy")){assert(v->kind==DATA);result=data(v->bytes,v->size);}
    else if(!strcmp(name,"mutableBytes")){assert(v->kind==DATA);result=v->bytes;}
    else if(!strcmp(name,"bytes")){assert(v->kind==DATA);result=v->bytes;}
    else if(!strcmp(name,"base64EncodedStringWithOptions:")){
#ifdef GB_DUAL_LOGIN
        assert(va_arg(args,Size)==0 && v->size==decoded_size+2+sizeof(region_qd_record));
        assert(!memcmp(v->bytes,decoded_gf,decoded_size));
        assert(v->bytes[decoded_size]==0x12 && v->bytes[decoded_size+1]==sizeof(region_qd_record));
        assert(!memcmp(v->bytes+decoded_size+2,region_qd_record,sizeof(region_qd_record)));
        struct Value *text=value(TEXT);text->text=fixture_dual_wire;result=text;
#else
        assert(va_arg(args,Size)==0 && v->size==decoded_size && !memcmp(v->bytes,decoded_qd,decoded_size));
        struct Value *text=value(TEXT);text->text=fixture_qd_wire;result=text;
#endif
    } else if(!strcmp(name,"dataUsingEncoding:")){
        assert(v->kind==TEXT && va_arg(args,Size)==4);struct Value *wire=data(v->text,(Size)strlen(v->text));
        if(encoded_size_mismatch)wire->bytes[wire->size++]='\n';result=wire;
    } else assert(!"Unexpected Foundation selector");
    va_end(args);return result;
}
static Obj get_association(Obj self,const void *key) {
    assert(self==&delegate && key==&region_buffer_key);return association;
}
static void set_association(Obj self,const void *key,Obj object,Size policy) {
    assert(self==&delegate && key==&region_buffer_key && policy==1);association=object;
}
static void original_data(Obj self,Sel command,Obj conn,Obj chunk) {
    assert(self==&delegate && conn==&connection && !strcmp(command,"connection:didReceiveData:"));
    struct Value *v=chunk;assert(v->kind==DATA && delivered_size+v->size<=sizeof(delivered));
    memcpy(delivered+delivered_size,v->bytes,v->size);delivered_size+=v->size;++received;
}
static void original_finish(Obj self,Sel command,Obj conn) {
    assert(self==&delegate && conn==&connection && !strcmp(command,"connectionDidFinishLoading:"));++finished;
}
static void original_fail(Obj self,Sel command,Obj conn,Obj error) {
    assert(self==&delegate && conn==&connection && error==(Obj)99 && !strcmp(command,"connection:didFailWithError:"));++failed;
}
static void reset(void) {
    used=received=finished=failed=buffer_allocation_failed=encoded_size_mismatch=0;delivered_size=0;association=0;
    host.text="dispatchcnglobal.yuanshen.com";
    get_class=klass;selector=sel;message=(Imp)msg;region_get_association=get_association;region_set_association=set_association;
    region_bypass=&bypass;region_next_data=original_data;region_next_finish=original_finish;region_next_fail=original_fail;
}
static void receive(Obj chunk) {region_data(&delegate,"connection:didReceiveData:",&connection,chunk);}
static void finish(void) {region_finish(&delegate,"connectionDidFinishLoading:",&connection);}
int main(void) {
    decoded_gf[0]=decoded_qd[0]=0x12;decoded_gf[1]=decoded_qd[1]=sizeof(region_gf_record);
    memcpy(decoded_gf+2,region_gf_record,sizeof(region_gf_record));memcpy(decoded_qd+2,region_qd_record,sizeof(region_qd_record));decoded_size=2+sizeof(region_gf_record);
    reset();receive(data(fixture_gf_wire,17));receive(data(fixture_gf_wire+17,(Size)strlen(fixture_gf_wire)-17));
    assert(!received);finish();assert(received==1 && finished==1 && !association);
    assert(delivered_size==strlen(fixture_qd_wire) && !memcmp(delivered,fixture_qd_wire,delivered_size));
    reset();host.text="unrelated.example";receive(data("PRIVATE",7));assert(received==1 && !association);finish();assert(finished==1);
    reset();receive(data("not-base64",10));finish();assert(received==1 && delivered_size==10 && !memcmp(delivered,"not-base64",10));
    reset();encoded_size_mismatch=1;receive(data(fixture_gf_wire,(Size)strlen(fixture_gf_wire)));finish();
    assert(delivered_size==strlen(fixture_gf_wire) && !memcmp(delivered,fixture_gf_wire,delivered_size));
    reset();receive(data("partial",7));region_fail(&delegate,"connection:didFailWithError:",&connection,(Obj)99);
    assert(!received && failed==1 && !association);
    reset();receive(data("x",1));receive(data(0,65537));assert(received==2 && association==region_bypass);
    receive(data("y",1));finish();assert(received==3 && finished==1 && !association);
    reset();buffer_allocation_failed=1;receive(data("a",1));assert(received==1 && association==region_bypass);
    buffer_allocation_failed=0;receive(data("b",1));finish();assert(received==2 && finished==1 && delivered_size==2);
    return 0;
}
