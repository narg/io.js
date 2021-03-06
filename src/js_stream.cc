#include "js_stream.h"

#include "async-wrap.h"
#include "env.h"
#include "env-inl.h"
#include "node_buffer.h"
#include "stream_base.h"
#include "stream_base-inl.h"
#include "v8.h"

namespace node {

using v8::Array;
using v8::Context;
using v8::External;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Local;
using v8::Object;
using v8::Value;


JSStream::JSStream(Environment* env, Handle<Object> obj, AsyncWrap* parent)
    : StreamBase(env),
      AsyncWrap(env, obj, AsyncWrap::PROVIDER_JSSTREAM, parent) {
  node::Wrap(obj, this);
  MakeWeak<JSStream>(this);
}


JSStream::~JSStream() {
}


void* JSStream::Cast() {
  return static_cast<void*>(this);
}


AsyncWrap* JSStream::GetAsyncWrap() {
  return static_cast<AsyncWrap*>(this);
}


bool JSStream::IsAlive() {
  return MakeCallback(env()->isalive_string(), 0, nullptr)->IsTrue();
}


bool JSStream::IsClosing() {
  return MakeCallback(env()->isclosing_string(), 0, nullptr)->IsTrue();
}


int JSStream::ReadStart() {
  return MakeCallback(env()->onreadstart_string(), 0, nullptr)->Int32Value();
}


int JSStream::ReadStop() {
  return MakeCallback(env()->onreadstop_string(), 0, nullptr)->Int32Value();
}


int JSStream::DoShutdown(ShutdownWrap* req_wrap) {
  HandleScope scope(env()->isolate());

  Local<Value> argv[] = {
    req_wrap->object()
  };

  req_wrap->Dispatched();
  Local<Value> res =
      MakeCallback(env()->onshutdown_string(), ARRAY_SIZE(argv), argv);

  return res->Int32Value();
}


int JSStream::DoWrite(WriteWrap* w,
                      uv_buf_t* bufs,
                      size_t count,
                      uv_stream_t* send_handle) {
  CHECK_EQ(send_handle, nullptr);

  HandleScope scope(env()->isolate());

  Local<Array> bufs_arr = Array::New(env()->isolate(), count);
  for (size_t i = 0; i < count; i++)
    bufs_arr->Set(i, Buffer::New(env(), bufs[i].base, bufs[i].len));

  Local<Value> argv[] = {
    w->object(),
    bufs_arr
  };

  w->Dispatched();
  Local<Value> res =
      MakeCallback(env()->onwrite_string(), ARRAY_SIZE(argv), argv);

  return res->Int32Value();
}


void JSStream::New(const FunctionCallbackInfo<Value>& args) {
  // This constructor should not be exposed to public javascript.
  // Therefore we assert that we are not trying to call this as a
  // normal function.
  CHECK(args.IsConstructCall());
  Environment* env = Environment::GetCurrent(args);
  JSStream* wrap;

  if (args.Length() == 0) {
    wrap = new JSStream(env, args.This(), nullptr);
  } else if (args[0]->IsExternal()) {
    void* ptr = args[0].As<External>()->Value();
    wrap = new JSStream(env, args.This(), static_cast<AsyncWrap*>(ptr));
  } else {
    UNREACHABLE();
  }
  CHECK(wrap);
}


static void FreeCallback(char* data, void* hint) {
  // Intentional no-op
}


void JSStream::DoAlloc(const FunctionCallbackInfo<Value>& args) {
  JSStream* wrap = Unwrap<JSStream>(args.Holder());

  uv_buf_t buf;
  wrap->OnAlloc(args[0]->Int32Value(), &buf);
  args.GetReturnValue().Set(Buffer::New(wrap->env(),
                                        buf.base,
                                        buf.len,
                                        FreeCallback,
                                        nullptr));
}


void JSStream::DoRead(const FunctionCallbackInfo<Value>& args) {
  JSStream* wrap = Unwrap<JSStream>(args.Holder());

  CHECK(Buffer::HasInstance(args[1]));
  uv_buf_t buf = uv_buf_init(Buffer::Data(args[1]), Buffer::Length(args[1]));
  wrap->OnRead(args[0]->Int32Value(), &buf);
}


void JSStream::DoAfterWrite(const FunctionCallbackInfo<Value>& args) {
  JSStream* wrap = Unwrap<JSStream>(args.Holder());
  WriteWrap* w = Unwrap<WriteWrap>(args[0].As<Object>());

  wrap->OnAfterWrite(w);
}


template <class Wrap>
void JSStream::Finish(const FunctionCallbackInfo<Value>& args) {
  Wrap* w = Unwrap<Wrap>(args[0].As<Object>());

  w->Done(args[1]->Int32Value());
}


void JSStream::ReadBuffer(const FunctionCallbackInfo<Value>& args) {
  JSStream* wrap = Unwrap<JSStream>(args.Holder());

  CHECK(Buffer::HasInstance(args[0]));
  char* data = Buffer::Data(args[0]);
  int len = Buffer::Length(args[0]);

  do {
    uv_buf_t buf;
    ssize_t avail = len;
    wrap->OnAlloc(len, &buf);
    if (static_cast<ssize_t>(buf.len) < avail)
      avail = buf.len;

    memcpy(buf.base, data, avail);
    data += avail;
    len -= avail;
    wrap->OnRead(avail, &buf);
  } while (len != 0);
}


void JSStream::EmitEOF(const FunctionCallbackInfo<Value>& args) {
  JSStream* wrap = Unwrap<JSStream>(args.Holder());

  wrap->OnRead(UV_EOF, nullptr);
}


void JSStream::Initialize(Handle<Object> target,
                          Handle<Value> unused,
                          Handle<Context> context) {
  Environment* env = Environment::GetCurrent(context);

  Local<FunctionTemplate> t = env->NewFunctionTemplate(New);
  t->SetClassName(FIXED_ONE_BYTE_STRING(env->isolate(), "JSStream"));
  t->InstanceTemplate()->SetInternalFieldCount(1);

  env->SetProtoMethod(t, "doAlloc", DoAlloc);
  env->SetProtoMethod(t, "doRead", DoRead);
  env->SetProtoMethod(t, "doAfterWrite", DoAfterWrite);
  env->SetProtoMethod(t, "finishWrite", Finish<WriteWrap>);
  env->SetProtoMethod(t, "finishShutdown", Finish<ShutdownWrap>);
  env->SetProtoMethod(t, "readBuffer", ReadBuffer);
  env->SetProtoMethod(t, "emitEOF", EmitEOF);

  StreamBase::AddMethods<JSStream>(env, t, StreamBase::kFlagHasWritev);
  target->Set(FIXED_ONE_BYTE_STRING(env->isolate(), "JSStream"),
              t->GetFunction());
  env->set_jsstream_constructor_template(t);
}

}  // namespace node

NODE_MODULE_CONTEXT_AWARE_BUILTIN(js_stream, node::JSStream::Initialize)
