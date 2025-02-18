/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <CommonBenchmarkSetup.h>
#include <CommonHierarchySetup.h>
#include <MockInterfaces.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/UnitTest/UnitTest.h>
#include <AzCore/Name/NameDictionary.h>
#include <AzCore/Name/Name.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/SpawnableSystemComponent.h>
#include <AzNetworking/UdpTransport/UdpPacketHeader.h>
#include <AzNetworking/Framework/NetworkingSystemComponent.h>
#include <AzTest/AzTest.h>
#include <MultiplayerSystemComponent.h>
#include <IMultiplayerConnectionMock.h>
#include <IMultiplayerSpawnerMock.h>
#include <ConnectionData/ServerToClientConnectionData.h>
#include <ReplicationWindows/ServerToClientReplicationWindow.h>
#include <Multiplayer/Components/NetBindComponent.h>
#include <Multiplayer/MultiplayerConstants.h>

namespace Multiplayer
{
    class MultiplayerSystemTests
        : public AllocatorsFixture
    {
    public:
        void SetUp() override
        {
            SetupAllocator();
            AZ::NameDictionary::Create();

            m_ComponentApplicationRequests = AZStd::make_unique<BenchmarkComponentApplicationRequests>();
            AZ::Interface<AZ::ComponentApplicationRequests>::Register(m_ComponentApplicationRequests.get());

            // register components involved in testing
            m_serializeContext = AZStd::make_unique<AZ::SerializeContext>();
            m_transformDescriptor.reset(AzFramework::TransformComponent::CreateDescriptor());
            m_transformDescriptor->Reflect(m_serializeContext.get());
            m_netBindDescriptor.reset(NetBindComponent::CreateDescriptor());
            m_netBindDescriptor->Reflect(m_serializeContext.get());

            m_netComponent = new AzNetworking::NetworkingSystemComponent();
            m_mpComponent = new Multiplayer::MultiplayerSystemComponent();

            m_initHandler = Multiplayer::SessionInitEvent::Handler([this](AzNetworking::INetworkInterface* value) { TestInitEvent(value); });
            m_mpComponent->AddSessionInitHandler(m_initHandler);
            m_shutdownHandler = Multiplayer::SessionShutdownEvent::Handler([this](AzNetworking::INetworkInterface* value) { TestShutdownEvent(value); });
            m_mpComponent->AddSessionShutdownHandler(m_shutdownHandler);
            m_connAcquiredHandler = Multiplayer::ConnectionAcquiredEvent::Handler([this](Multiplayer::MultiplayerAgentDatum value) { TestConnectionAcquiredEvent(value); });
            m_mpComponent->AddConnectionAcquiredHandler(m_connAcquiredHandler);
            m_endpointDisconnectedHandler = Multiplayer::EndpointDisconnectedEvent::Handler([this](Multiplayer::MultiplayerAgentType value) { TestEndpointDisconnectedEvent(value); });
            m_mpComponent->AddEndpointDisconnectedHandler(m_endpointDisconnectedHandler);
            m_mpComponent->Activate();
        }

        void TearDown() override
        {
            m_mpComponent->Deactivate();
            delete m_mpComponent;
            delete m_netComponent;
            AZ::Interface<AZ::ComponentApplicationRequests>::Unregister(m_ComponentApplicationRequests.get());
            m_ComponentApplicationRequests.reset();
            AZ::NameDictionary::Destroy();

            m_transformDescriptor.reset();
            m_netBindDescriptor.reset();
            m_serializeContext.reset();

            TeardownAllocator();
        }

        void TestInitEvent([[maybe_unused]] AzNetworking::INetworkInterface* network)
        {
            ++m_initEventTriggerCount;
        }

        void TestShutdownEvent([[maybe_unused]] AzNetworking::INetworkInterface* network)
        {
            ++m_shutdownEventTriggerCount;
        }

        void TestConnectionAcquiredEvent(Multiplayer::MultiplayerAgentDatum& datum)
        {
            m_connectionAcquiredCount += aznumeric_cast<uint32_t>(datum.m_id);
        }

        void TestEndpointDisconnectedEvent([[maybe_unused]] Multiplayer::MultiplayerAgentType value)
        {
            ++m_endpointDisconnectedCount;
        }

        void CreateAndRegisterNetBindComponent(AZ::Entity& playerEntity, NetworkEntityTracker& playerNetworkEntityTracker, NetEntityRole netEntityRole)
        {
            playerEntity.CreateComponent<AzFramework::TransformComponent>();
            const auto playerNetBindComponent = playerEntity.CreateComponent<NetBindComponent>();
            playerNetBindComponent->m_netEntityRole = netEntityRole;
            playerNetworkEntityTracker.RegisterNetBindComponent(&playerEntity, playerNetBindComponent);
            playerEntity.Init();
            playerEntity.Activate();
        }

        AZStd::unique_ptr<AZ::SerializeContext> m_serializeContext;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_transformDescriptor;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_netBindDescriptor;

        uint32_t m_initEventTriggerCount = 0;
        uint32_t m_shutdownEventTriggerCount = 0;
        uint32_t m_connectionAcquiredCount = 0;
        uint32_t m_endpointDisconnectedCount = 0;

        Multiplayer::SessionInitEvent::Handler m_initHandler;
        Multiplayer::SessionShutdownEvent::Handler m_shutdownHandler;
        Multiplayer::ConnectionAcquiredEvent::Handler m_connAcquiredHandler;
        Multiplayer::EndpointDisconnectedEvent::Handler m_endpointDisconnectedHandler;

        AzNetworking::NetworkingSystemComponent* m_netComponent = nullptr;
        Multiplayer::MultiplayerSystemComponent* m_mpComponent = nullptr;

        AZStd::unique_ptr<BenchmarkComponentApplicationRequests> m_ComponentApplicationRequests; 

        IMultiplayerSpawnerMock m_mpSpawnerMock;
    };

    TEST_F(MultiplayerSystemTests, TestInitEvent)
    {
        m_mpComponent->InitializeMultiplayer(MultiplayerAgentType::DedicatedServer);
        EXPECT_EQ(m_mpComponent->GetAgentType(), MultiplayerAgentType::DedicatedServer);

        m_mpComponent->InitializeMultiplayer(MultiplayerAgentType::ClientServer);
        EXPECT_EQ(m_mpComponent->GetAgentType(), MultiplayerAgentType::ClientServer);

        m_mpComponent->InitializeMultiplayer(MultiplayerAgentType::Client);
        EXPECT_EQ(m_mpComponent->GetAgentType(), MultiplayerAgentType::Client);

        EXPECT_EQ(m_initEventTriggerCount, 1);
    }

    TEST_F(MultiplayerSystemTests, TestShutdownEvent)
    {
        m_mpComponent->InitializeMultiplayer(Multiplayer::MultiplayerAgentType::DedicatedServer);
        IMultiplayerConnectionMock connMock1 = IMultiplayerConnectionMock(AzNetworking::ConnectionId(), AzNetworking::IpAddress(), AzNetworking::ConnectionRole::Acceptor);
        IMultiplayerConnectionMock connMock2 = IMultiplayerConnectionMock(AzNetworking::ConnectionId(), AzNetworking::IpAddress(), AzNetworking::ConnectionRole::Connector);
        m_mpComponent->OnDisconnect(&connMock1, AzNetworking::DisconnectReason::None, AzNetworking::TerminationEndpoint::Local);
        m_mpComponent->OnDisconnect(&connMock2, AzNetworking::DisconnectReason::None, AzNetworking::TerminationEndpoint::Local);

        EXPECT_EQ(m_endpointDisconnectedCount, 2);
        EXPECT_EQ(m_shutdownEventTriggerCount, 1);
    }

    TEST_F(MultiplayerSystemTests, TestConnectionDatum)
    {
        using namespace testing;
        NiceMock<IMultiplayerConnectionMock> connMock1(aznumeric_cast<AzNetworking::ConnectionId>(10), AzNetworking::IpAddress(), AzNetworking::ConnectionRole::Acceptor);
        NiceMock<IMultiplayerConnectionMock> connMock2(aznumeric_cast<AzNetworking::ConnectionId>(15), AzNetworking::IpAddress(), AzNetworking::ConnectionRole::Acceptor);
        m_mpComponent->OnConnect(&connMock1);
        m_mpComponent->OnConnect(&connMock2);

        EXPECT_EQ(m_connectionAcquiredCount, 25);

        // Clean up connection data
        m_mpComponent->OnDisconnect(&connMock1, AzNetworking::DisconnectReason::None, AzNetworking::TerminationEndpoint::Local);
        m_mpComponent->OnDisconnect(&connMock2, AzNetworking::DisconnectReason::None, AzNetworking::TerminationEndpoint::Local);

        EXPECT_EQ(m_endpointDisconnectedCount, 2);
    }

    TEST_F(MultiplayerSystemTests, TestSpawnerEvents)
    {
        AZ::Interface<Multiplayer::IMultiplayerSpawner>::Register(&m_mpSpawnerMock);
        m_mpComponent->InitializeMultiplayer(Multiplayer::MultiplayerAgentType::ClientServer);

        AZ_TEST_START_TRACE_SUPPRESSION;
        // Setup mock connection and dummy connection data, this should raise two errors around entity validity
        Multiplayer::NetworkEntityHandle controlledEntity;
        IMultiplayerConnectionMock connMock =
            IMultiplayerConnectionMock(AzNetworking::ConnectionId(), AzNetworking::IpAddress(), AzNetworking::ConnectionRole::Acceptor);
        Multiplayer::ServerToClientConnectionData* connectionData = new Multiplayer::ServerToClientConnectionData(&connMock, *m_mpComponent);
        connectionData->GetReplicationManager().SetReplicationWindow(AZStd::make_unique<Multiplayer::ServerToClientReplicationWindow>(controlledEntity, &connMock));
        connMock.SetUserData(connectionData);

        m_mpComponent->OnDisconnect(&connMock, AzNetworking::DisconnectReason::None, AzNetworking::TerminationEndpoint::Local);
        AZ_TEST_STOP_TRACE_SUPPRESSION(2);

        EXPECT_EQ(m_endpointDisconnectedCount, 1);
        EXPECT_EQ(m_mpSpawnerMock.m_playerCount, 0);
        AZ::Interface<Multiplayer::IMultiplayerSpawner>::Unregister(&m_mpSpawnerMock);
    }

    TEST_F(MultiplayerSystemTests, TestClientServerConnectingWithoutPlayerEntity)
    {
        AZ::Interface<IMultiplayerSpawner>::Register(&m_mpSpawnerMock);

        m_mpSpawnerMock.m_networkEntityHandle = NetworkEntityHandle();
        EXPECT_FALSE(m_mpSpawnerMock.m_networkEntityHandle.Exists());

        m_mpComponent->InitializeMultiplayer(MultiplayerAgentType::ClientServer);
        EXPECT_EQ(m_mpSpawnerMock.m_playerEntityRequestedCount, 1);

        // We don't have a player entity yet, so MultiplayerSystemComponent should request another player entity when root spawnable (a new level) is finished loading
        AzFramework::RootSpawnableNotificationBus::Broadcast(&AzFramework::RootSpawnableNotificationBus::Events::OnRootSpawnableReady, AZ::Data::Asset<AzFramework::Spawnable>(), 0);
        EXPECT_EQ(m_mpSpawnerMock.m_playerEntityRequestedCount, 2);

        AZ::Interface<IMultiplayerSpawner>::Unregister(&m_mpSpawnerMock);
    }

    TEST_F(MultiplayerSystemTests, TestClientServerConnectingWithPlayerEntity)
    {
        AZ::Interface<IMultiplayerSpawner>::Register(&m_mpSpawnerMock);

        // Setup a net player entity
        AZ::Entity playerEntity;
        NetworkEntityTracker playerNetworkEntityTracker;
        CreateAndRegisterNetBindComponent(playerEntity, playerNetworkEntityTracker, NetEntityRole::Authority);
        m_mpSpawnerMock.m_networkEntityHandle = NetworkEntityHandle(&playerEntity, &playerNetworkEntityTracker);
        EXPECT_TRUE(m_mpSpawnerMock.m_networkEntityHandle.Exists());

        // Initialize the ClientServer (this will also spawn a player for the host)
        m_mpComponent->InitializeMultiplayer(MultiplayerAgentType::ClientServer);
        EXPECT_EQ(m_mpSpawnerMock.m_playerEntityRequestedCount, 1);

        // Send a connection request. This should cause another player to be spawned.
        MultiplayerPackets::Connect connectPacket(0, 1, "connect_ticket");
        IMultiplayerConnectionMock connection(
            ConnectionId{ 1 }, IpAddress("127.0.0.1", DefaultServerPort, ProtocolType::Udp), ConnectionRole::Connector);
        ServerToClientConnectionData connectionUserData(&connection, *m_mpComponent);
        connection.SetUserData(&connectionUserData);

        m_mpComponent->HandleRequest(&connection, UdpPacketHeader(), connectPacket);

        EXPECT_EQ(m_mpSpawnerMock.m_playerEntityRequestedCount, 2);

        // Players should already be created and we should not request another player entity when root spawnable (a new
        // level) is finished loading
        AzFramework::RootSpawnableNotificationBus::Broadcast(
            &AzFramework::RootSpawnableNotificationBus::Events::OnRootSpawnableReady, AZ::Data::Asset<AzFramework::Spawnable>(), 0);
        EXPECT_EQ(m_mpSpawnerMock.m_playerEntityRequestedCount, 2); // player count is still 2 (stays the same)

        AZ::Interface<IMultiplayerSpawner>::Unregister(&m_mpSpawnerMock);
    }
}
