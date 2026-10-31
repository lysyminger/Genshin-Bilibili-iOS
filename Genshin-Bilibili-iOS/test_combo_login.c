#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#define __attribute__(attributes)
#define GB_ENABLE_BRIDGE 1
#define GB_ENABLE_BILI_COMPONENT 1
#define GB_ENABLE_COMBO_LOGIN 1
#include "src/GBDiagnostic.c"
#undef __attribute__

void *dlsym(void *handle, const char *name) { (void)handle; (void)name; return 0; }
enum { CSTRING=1, CDICT, CMDICT, CNUMBER, CCONFIG, CTOOL, CNET, CLOGIN, CCALLBACK, CURL, CSIGN };
struct Value { int kind; const char *text; long long number; int count;
    struct { const char *key; Obj value; } pairs[32]; };
static struct Value values[512];
static int used, requests, callbacks, official_calls, server_side_effects, last_index;
static int channel;
static Obj active_combo, request_body, serialized_request, serialized_callback, endpoint;
static struct GBComboBlock retained_block;
static Obj new_value(int kind) { assert(used<512); struct Value *v=&values[used++]; memset(v,0,sizeof(*v));v->kind=kind;return v; }
static Obj str(const char *s) { Obj v=new_value(CSTRING);((struct Value *)v)->text=s;return v; }
static Obj num(long long n) { Obj v=new_value(CNUMBER);((struct Value *)v)->number=n;return v; }
static Obj dict(void) { return new_value(CDICT); }
static void put(Obj object,const char *key,Obj value) {
    struct Value *v=object;assert(v && v->kind==CDICT);
    for(int i=0;i<v->count;++i)if(strcmp(v->pairs[i].key,key)==0){v->pairs[i].value=value;return;}
    assert(v->count<32);v->pairs[v->count].key=key;v->pairs[v->count++].value=value;
}
static Obj get(Obj object,const char *key) {
    if(!object)return 0;struct Value *v=object;assert(v->kind==CDICT);
    for(int i=0;i<v->count;++i)if(strcmp(v->pairs[i].key,key)==0)return v->pairs[i].value;
    return 0;
}
static Obj klass(const char *s) {
    const char *names[]={"","NSString","NSDictionary","NSMutableDictionary","NSNumber","MHYConfig",
        "MiHoYoSDKTool","MiHoYoNetworkManager","MiHoYoComboLogin","MiHoYoComboCallback","MiHoYoURLConfig","MiHoYoHmacSHA256Encryptor"};
    for(int i=1;i<=CSIGN;++i)if(strcmp(s,names[i])==0)return (Obj)(__UINTPTR_TYPE__)i;
    return 0;
}
static Sel sel(const char *s) { return (Sel)s; }
static Obj retain(Obj o) { return o; }
static void release(Obj o) { (void)o; }
static void dispatch(void *q,void *context,void (*fn)(void *)) { (void)q;fn(context); }
static void original(Obj s,Sel c,Obj f,Obj a,int i) { (void)s;(void)c;(void)f;(void)a;(void)i;++official_calls; }
static Obj return_original(Obj s,Sel c,Obj f,Obj a) { (void)s;(void)c;(void)f;(void)a;return str("1"); }
static Obj msg(Obj object,Sel method,...) {
    if(!object)return 0;
    const char *name=method;va_list ap;va_start(ap,method);Obj result=0;
    __UINTPTR_TYPE__ c=(__UINTPTR_TYPE__)object;
    struct Value *v=c>CSIGN?object:0;
    if(strcmp(name,"respondsToSelector:")==0){result=(Obj)1;goto done;}
    if(strcmp(name,"isKindOfClass:")==0){int k=(int)(__UINTPTR_TYPE__)va_arg(ap,Obj);result=(Obj)(__UINTPTR_TYPE__)(v && (v->kind==k || (v->kind==CDICT && k==CMDICT)));goto done;}
    if(strcmp(name,"instance")==0){result=object;goto done;}
    if(c==CSTRING && strcmp(name,"stringWithUTF8String:")==0){result=str(va_arg(ap,const char *));goto done;}
    if(c==CNUMBER && strcmp(name,"numberWithLongLong:")==0){result=num(va_arg(ap,long long));goto done;}
    if(c==CNUMBER && strcmp(name,"numberWithBool:")==0){result=num(va_arg(ap,int));goto done;}
    if(c==CMDICT && strcmp(name,"dictionary")==0){result=dict();goto done;}
    if(v && strcmp(name,"UTF8String")==0){result=(Obj)v->text;goto done;}
    if(v && strcmp(name,"length")==0){result=(Obj)(__UINTPTR_TYPE__)strlen(v->text);goto done;}
    if(v && (strcmp(name,"intValue")==0 || strcmp(name,"longLongValue")==0)){
        result=(Obj)(__UINTPTR_TYPE__)(v->kind==CNUMBER?v->number:atoll(v->text));goto done;}
    if(v && strcmp(name,"objectForKey:")==0){struct Value *k=va_arg(ap,Obj);result=get(object,k->text);goto done;}
    if(v && strcmp(name,"setObject:forKey:")==0){Obj val=va_arg(ap,Obj);struct Value *key=va_arg(ap,Obj);put(object,key->text,val);goto done;}
    if(v && strcmp(name,"removeObjectForKey:")==0){struct Value *k=va_arg(ap,Obj);put(object,k->text,0);goto done;}
    if(v && strcmp(name,"mutableCopy")==0){result=new_value(CDICT);*(struct Value *)result=*v;goto done;}
    if(c==CCONFIG && strcmp(name,"comboAppid")==0){result=str("4");goto done;}
    if(c==CCONFIG && strcmp(name,"comboAppkey")==0){result=str("synthetic-app-key");goto done;}
    if(c==CCONFIG && strcmp(name,"channelID")==0){result=(Obj)(__UINTPTR_TYPE__)channel;goto done;}
    if(c==CCONFIG && strcmp(name,"setChannelID:")==0){channel=va_arg(ap,int);goto done;}
    if(c==CCONFIG && strcmp(name,"setCurrentComboDic:")==0){active_combo=va_arg(ap,Obj);goto done;}
    if(c==CURL && strcmp(name,"comboLogin")==0){result=endpoint;goto done;}
    if(c==CTOOL && strcmp(name,"getDeviceID")==0){result=str("synthetic-device-id");goto done;}
    if(c==CTOOL && strcmp(name,"dictionaryToJsonData:")==0){
        Obj d=va_arg(ap,Obj);
        if(get(d,"uid")){assert(get(d,"access_key"));result=str("synthetic-serialized-data");}
        else if(get(d,"sign")){assert(get(d,"app_id") && get(d,"channel_id") && get(d,"device") && get(d,"data"));
             serialized_request=d;result=str("synthetic-serialized-request");}
        else{assert(get(d,"ret"));serialized_callback=d;result=str("synthetic-serialized-callback");}
        goto done;}
    if(c==CSIGN && strcmp(name,"sign:secret:")==0){
        Obj d=va_arg(ap,Obj);(void)va_arg(ap,Obj);assert(((struct Value *)get(d,"channel_id"))->number==14);
        assert(get(d,"app_id") && get(d,"device") && get(d,"data"));result=str("synthetic-signature");goto done;}
    if(c==CNET && strcmp(name,"postRequest:body:timeout:callback:")==0){
        assert(va_arg(ap,Obj)==endpoint);struct Value *wire=va_arg(ap,Obj);
        assert(wire->kind==CSTRING && strcmp(wire->text,"synthetic-serialized-request")==0);
        request_body=serialized_request;assert(request_body && va_arg(ap,double)==30.0);
        retained_block=*(struct GBComboBlock *)va_arg(ap,void *);++requests;goto done;}
    if(c==CNET && strcmp(name,"handle:response:error:")==0){result=va_arg(ap,Obj);goto done;}
    if(c==CLOGIN && strcmp(name,"loginSuccess:response:")==0){
        assert(va_arg(ap,Obj)==0);Obj response=va_arg(ap,Obj);active_combo=get(response,"data");++server_side_effects;goto done;}
    if(c==CCALLBACK && strcmp(name,"callBack:response:index:")==0){
        struct Value *f=va_arg(ap,Obj);struct Value *wire=va_arg(ap,Obj);last_index=va_arg(ap,int);
        assert(wire->kind==CSTRING && strcmp(wire->text,"synthetic-serialized-callback")==0);
        Obj response=serialized_callback;assert(response);
        assert(strcmp(f->text,"login_login")==0 || strcmp(f->text,"pay_pay")==0);
        if(((struct Value *)get(response,"ret"))->number==0){assert(get(get(response,"data"),"combo_token"));++callbacks;}
        goto done;}
done:va_end(ap);return result;
}
static void reset(void) {
    used=requests=callbacks=official_calls=server_side_effects=0;channel=1;active_combo=serialized_request=serialized_callback=0;
    get_class=klass;selector=sel;message=(Imp)msg;bridge_retain=retain;bridge_release=release;
    bridge_dispatch=dispatch;bridge_next_invoke=original;combo_next_return=return_original;
    combo_allocate=(void *)malloc;combo_free=free;combo_stack_block_class=(void *)1;combo_ready=1;
    static struct {void *isa;unsigned long generation;} delegate;
    delegate.generation=17;bili_delegate=&delegate;bili_generation_offset=sizeof(void *);bili_sdk=0;bili_finishing=0;
    bridge=(GBLoginBridge){GB_VERIFYING,GB_BILIBILI,17};combo_channel_active=0;
    pending_self=(Obj)1000;pending_function=0;pending_args=0;pending_index=77;
    endpoint=str("https://hk4e-sdk.mihoyo.com/hk4e_cn/combo/granter/login/v2/login");
}
static Obj response(int code,int token_present,int status_present) {
    Obj wrapper=dict(),server=dict(),data=dict();put(wrapper,"ret",num(0));put(wrapper,"data",server);
    if(status_present)put(server,"retcode",num(code));put(server,"data",data);
    put(data,"open_id",str("synthetic-open-id"));put(data,"account_type",num(1));
    if(token_present)put(data,"combo_token",str("synthetic-combo-token"));return wrapper;
}
int main(void) {
    reset();assert(!combo_dispatch_route_active());
    combo_begin(bili_delegate,str("123"),str("synthetic-access-key"));
    assert(!combo_dispatch_route_active());
    assert(requests==1 && channel==14 && get(request_body,"sign"));
    assert(retained_block.generation==17); /* callback is copied after begin's stack has returned */
    combo_network_callback(&retained_block,response(0,1,1),0,0);
    assert(callbacks==1 && server_side_effects==1 && last_index==77);
    assert(bridge.phase==GB_AUTHENTICATED && active_combo && pending_self==0);
    assert(combo_dispatch_route_active());
    bridge.channel=GB_OFFICIAL;assert(!combo_dispatch_route_active());bridge.channel=GB_BILIBILI;
    assert(get(active_combo,"name") && get(active_combo,"open_token") && get(active_combo,"asterisk_name"));
    assert(get(active_combo,"guest") && get(active_combo,"is_new_register"));
    combo_network_callback(&retained_block,response(0,1,1),0,0);assert(callbacks==1);
    combo_restore_official();assert(!combo_dispatch_route_active());
    reset();combo_begin(bili_delegate,str("123"),str("synthetic-access-key"));
    combo_network_callback(&retained_block,response(-1,1,1),0,0);
    assert(callbacks==0 && server_side_effects==0 && bridge.phase!=GB_AUTHENTICATED);
    reset();combo_begin(bili_delegate,str("123"),str("synthetic-access-key"));
    combo_network_callback(&retained_block,response(0,0,1),0,0);assert(callbacks==0 && server_side_effects==0);
    reset();combo_begin(bili_delegate,str("123"),str("synthetic-access-key"));
    combo_network_callback(&retained_block,response(0,1,0),0,0);assert(callbacks==0 && server_side_effects==0);
    reset();endpoint=str("https://untrusted.invalid/login");
    combo_begin(bili_delegate,str("123"),str("synthetic-access-key"));assert(requests==0 && callbacks==0);
    reset();combo_begin(bili_delegate,str("123"),str("synthetic-access-key"));++bridge.generation;
    combo_network_callback(&retained_block,response(0,1,1),0,0);assert(callbacks==0 && server_side_effects==0);
    reset();combo_channel_active=1;
    Obj route=combo_return(0,0,str("info_get_channel_id"),0);assert(strcmp(((struct Value *)route)->text,"14")==0);
    assert(combo_handle_invoke(0,0,str("pay_pay"),0,77)==1 && requests==0);
#ifdef GB_BILI_REGION_ONLY
    reset(); bridge.phase=GB_CHOOSING; pending_function=str("login_login");
    bridge_resume_official(0,0);
    assert(bridge.phase==GB_IDLE && pending_self==0 && pending_function==0);
    assert(last_index==77 && callbacks==0 && official_calls==0 && requests==0);
#endif
    return 0;
}
