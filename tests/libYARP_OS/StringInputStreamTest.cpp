/*
 * Copyright (C) 2006 RobotCub Consortium
 * Authors: Paul Fitzpatrick
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */

#include <yarp/os/StringInputStream.h>

#include <yarp/os/impl/UnitTest.h>
//#include "TestList.h"

using namespace yarp::os;
using namespace yarp::os::impl;

class StringInputStreamTest : public UnitTest {
public:
    virtual ConstString getName() override { return "StringInputStreamTest"; }

    void testRead() {
        report(0,"test reading...");

        StringInputStream sis;
        sis.add("Hello my friend");
        char buf[256];
        sis.check();
        Bytes b(buf,sizeof(buf));
        int len = sis.read(b,0,5);
        checkEqual(len,5,"len of first read");
        buf[len] = '\0';
        checkEqual("Hello",buf,"first read");
        char ch = sis.read();
        checkEqual(ch,' ',"the space");
        len = sis.read(b,0,2);
        checkEqual(len,2,"len of second read");
        buf[len] = '\0';
        checkEqual("my",buf,"second read");
    }

    virtual void runTests() override {
        testRead();
    }
};

static StringInputStreamTest theStringInputStreamTest;

UnitTest& getStringInputStreamTest() {
    return theStringInputStreamTest;
}

