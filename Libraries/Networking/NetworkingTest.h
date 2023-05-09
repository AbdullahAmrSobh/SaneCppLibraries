// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#pragma once
#include "../Testing/Test.h"
#include "SocketDescriptor.h"

namespace SC
{
struct NetworkingTest;
}

struct SC::NetworkingTest : public SC::TestCase
{
    NetworkingTest(SC::TestReport& report) : TestCase(report, "NetworkingTest")
    {
        using namespace SC;
        if (test_section("socket"))
        {
            bool isInheritable;

            // We are testing only the inheritable because on windows there is no reliable
            // way of checking if a non-connected socket is in non-blocking mode
            SocketDescriptor socket;
            SC_TEST_EXPECT(socket.create(SocketFlags::AddressFamilyIPV4, SocketFlags::SocketStream,
                                         SocketFlags::ProtocolTcp, DescriptorFlags::NonBlocking,
                                         DescriptorFlags::NonInheritable));
            SC_TEST_EXPECT(socket.isValid());
            isInheritable = false;
            SC_TEST_EXPECT(socket.isInheritable(isInheritable));
            SC_TEST_EXPECT(not isInheritable);
            SC_TEST_EXPECT(socket.close());

            SC_TEST_EXPECT(socket.create(SocketFlags::AddressFamilyIPV4, SocketFlags::SocketStream,
                                         SocketFlags::ProtocolTcp, DescriptorFlags::Blocking,
                                         DescriptorFlags::NonInheritable));
            SC_TEST_EXPECT(socket.isValid());
            isInheritable = false;
            SC_TEST_EXPECT(socket.isInheritable(isInheritable));
            SC_TEST_EXPECT(not isInheritable);
            SC_TEST_EXPECT(socket.close());

            SC_TEST_EXPECT(socket.create(SocketFlags::AddressFamilyIPV4, SocketFlags::SocketStream,
                                         SocketFlags::ProtocolTcp, DescriptorFlags::Blocking,
                                         DescriptorFlags::Inheritable));
            SC_TEST_EXPECT(socket.isValid());
            isInheritable = false;
            SC_TEST_EXPECT(socket.isInheritable(isInheritable));
            SC_TEST_EXPECT(isInheritable);
            SC_TEST_EXPECT(socket.close());
        }
        if (test_section("tcp client server"))
        {
            SocketDescriptor serverSocket;
            SocketServer     server(serverSocket);
            // Look for an available port
            constexpr int    startTcpPort = 5050;
            uint16_t         tcpPort;
            ReturnCode       bound         = true;
            const StringView serverAddress = "::1"; //"127.0.0.1"
            for (tcpPort = startTcpPort; tcpPort < startTcpPort + 10; ++tcpPort)
            {
                bound = server.listen(serverAddress, tcpPort);
                if (bound)
                {
                    break;
                }
            }
            SC_TEST_EXPECT(bound);
            constexpr char testValue = 123;
            struct Params
            {
                ReturnCode  connectRes = false;
                ReturnCode  writeRes   = false;
                ReturnCode  closeRes   = false;
                EventObject eventObject;
            } params;
            Action func = [&]()
            {
                SocketDescriptor clientSocket;
                SocketClient     client(clientSocket);
                params.connectRes = client.connect(serverAddress, tcpPort);
                char buf[1]       = {testValue};
                params.writeRes   = client.write({buf, sizeof(buf)});
                params.eventObject.wait();
                buf[0]++;
                params.writeRes = client.write({buf, sizeof(buf)});
                params.eventObject.wait();
                params.closeRes = client.close();
            };
            Thread thread;
            SC_TEST_EXPECT(thread.start("tcp", &func));
            SocketFlags::AddressFamily family;
            SC_TEST_EXPECT(serverSocket.getAddressFamily(family));
            SocketDescriptor acceptedClientSocket;
            SC_TEST_EXPECT(server.accept(family, acceptedClientSocket));
            SC_TEST_EXPECT(acceptedClientSocket.isValid());
            char         buf[1] = {0};
            SocketClient acceptedClient(acceptedClientSocket);
            SC_TEST_EXPECT(acceptedClient.read({buf, sizeof(buf)}));
            SC_TEST_EXPECT(buf[0] == testValue and testValue != 0);
            SC_TEST_EXPECT(not acceptedClient.readWithTimeout({buf, sizeof(buf)}, 10_ms));
            params.eventObject.signal();
            SC_TEST_EXPECT(acceptedClient.readWithTimeout({buf, sizeof(buf)}, 10000_ms));
            SC_TEST_EXPECT(buf[0] == testValue + 1);
            SC_TEST_EXPECT(acceptedClient.close());
            SC_TEST_EXPECT(server.close());
            params.eventObject.signal();
            SC_TEST_EXPECT(thread.join());
            SC_TEST_EXPECT(params.connectRes and params.writeRes and params.closeRes);
        }
    }
};