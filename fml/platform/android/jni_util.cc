// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/platform/android/jni_util.h"

#include <codecvt>
#include <string>

#include "lib/ftl/logging.h"

namespace fml {
namespace jni {

static JavaVM* g_jvm = nullptr;
static ScopedJavaGlobalRef<jobject>* g_android_application_context = nullptr;

#define ASSERT_NO_EXCEPTION() FTL_CHECK(env->ExceptionCheck() == JNI_FALSE);

void InitJavaVM(JavaVM* vm) {
  FTL_DCHECK(g_jvm == nullptr);
  g_jvm = vm;
}

JNIEnv* AttachCurrentThread() {
  FTL_DCHECK(g_jvm != nullptr)
      << "Trying to attach to current thread without calling InitJavaVM first.";
  JNIEnv* env = nullptr;
  jint ret = g_jvm->AttachCurrentThread(&env, nullptr);
  FTL_DCHECK(JNI_OK == ret);
  return env;
}

void DetachFromVM() {
  if (g_jvm) {
    g_jvm->DetachCurrentThread();
  }
}

void InitAndroidApplicationContext(const JavaRef<jobject>& context) {
  FTL_DCHECK(g_android_application_context == nullptr);
  g_android_application_context = new ScopedJavaGlobalRef<jobject>(context);
  FTL_DCHECK(g_android_application_context->obj() != nullptr);
}

const jobject GetAndroidApplicationContext() {
  jobject object = g_android_application_context->obj();
  FTL_DCHECK(object != nullptr)
      << "Trying to get Android application context without first calling "
         "InitAndroidApplicationContext.";
  return object;
}

static std::string UTF16StringToUTF8String(const char16_t* chars, size_t len) {
  std::u16string u16_string(chars, len);
  return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}
      .to_bytes(u16_string);
}

std::string JavaStringToString(JNIEnv* env, jstring str) {
  if (env == nullptr || str == nullptr) {
    return "";
  }
  const jchar* chars = env->GetStringChars(str, NULL);
  if (chars == nullptr) {
    return "";
  }
  std::string u8_string = UTF16StringToUTF8String(
      reinterpret_cast<const char16_t*>(chars), env->GetStringLength(str));
  env->ReleaseStringChars(str, chars);
  ASSERT_NO_EXCEPTION();
  return u8_string;
}

static std::u16string UTF8StringToUTF16String(const std::string& string) {
  return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}
      .from_bytes(string);
}

ScopedJavaLocalRef<jstring> StringToJavaString(JNIEnv* env,
                                               const std::string& u8_string) {
  std::u16string u16_string = UTF8StringToUTF16String(u8_string);
  auto result = ScopedJavaLocalRef<jstring>(
      env, env->NewString(reinterpret_cast<const jchar*>(u16_string.data()),
                          u16_string.length()));
  ASSERT_NO_EXCEPTION();
  return result;
}

std::vector<std::string> StringArrayToVector(JNIEnv* env, jobjectArray array) {
  std::vector<std::string> out;
  if (env == nullptr || array == nullptr) {
    return out;
  }

  jsize length = env->GetArrayLength(array);

  if (length == -1) {
    return out;
  }

  out.resize(length);
  for (size_t i = 0; i < length; ++i) {
    ScopedJavaLocalRef<jstring> java_string(
        env, static_cast<jstring>(env->GetObjectArrayElement(array, i)));
    out[i] = JavaStringToString(env, java_string.obj());
  }

  return out;
}

ScopedJavaLocalRef<jobjectArray> VectorToStringArray(
    JNIEnv* env,
    const std::vector<std::string>& vector) {
  FTL_DCHECK(env);
  ScopedJavaLocalRef<jclass> string_clazz(env,
                                          env->FindClass("java/lang/String"));
  FTL_DCHECK(!string_clazz.is_null());
  jobjectArray joa =
      env->NewObjectArray(vector.size(), string_clazz.obj(), NULL);
  ASSERT_NO_EXCEPTION();
  for (size_t i = 0; i < vector.size(); ++i) {
    ScopedJavaLocalRef<jstring> item = StringToJavaString(env, vector[i]);
    env->SetObjectArrayElement(joa, i, item.obj());
  }
  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

bool HasException(JNIEnv* env) {
  return env->ExceptionCheck() != JNI_FALSE;
}

bool ClearException(JNIEnv* env) {
  if (!HasException(env))
    return false;
  env->ExceptionDescribe();
  env->ExceptionClear();
  return true;
}

std::string GetJavaExceptionInfo(JNIEnv* env, jthrowable java_throwable) {
  ScopedJavaLocalRef<jclass> throwable_clazz(
      env, env->FindClass("java/lang/Throwable"));

  jmethodID throwable_printstacktrace = env->GetMethodID(
      throwable_clazz.obj(), "printStackTrace", "(Ljava/io/PrintStream;)V");

  // Create an instance of ByteArrayOutputStream.
  ScopedJavaLocalRef<jclass> bytearray_output_stream_clazz(
      env, env->FindClass("java/io/ByteArrayOutputStream"));

  jmethodID bytearray_output_stream_constructor =
      env->GetMethodID(bytearray_output_stream_clazz.obj(), "<init>", "()V");
  jmethodID bytearray_output_stream_tostring = env->GetMethodID(
      bytearray_output_stream_clazz.obj(), "toString", "()Ljava/lang/String;");
  ScopedJavaLocalRef<jobject> bytearray_output_stream(
      env, env->NewObject(bytearray_output_stream_clazz.obj(),
                          bytearray_output_stream_constructor));

  // Create an instance of PrintStream.
  ScopedJavaLocalRef<jclass> printstream_clazz(
      env, env->FindClass("java/io/PrintStream"));

  jmethodID printstream_constructor = env->GetMethodID(
      printstream_clazz.obj(), "<init>", "(Ljava/io/OutputStream;)V");
  ScopedJavaLocalRef<jobject> printstream(
      env, env->NewObject(printstream_clazz.obj(), printstream_constructor,
                          bytearray_output_stream.obj()));

  // Call Throwable.printStackTrace(PrintStream)
  env->CallVoidMethod(java_throwable, throwable_printstacktrace,
                      printstream.obj());

  // Call ByteArrayOutputStream.toString()
  ScopedJavaLocalRef<jstring> exception_string(
      env,
      static_cast<jstring>(env->CallObjectMethod(
          bytearray_output_stream.obj(), bytearray_output_stream_tostring)));

  return JavaStringToString(env, exception_string.obj());
}

}  // namespace jni
}  // namespace fml