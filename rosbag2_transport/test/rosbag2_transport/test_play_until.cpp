// Copyright 2022, Open Source Robotics Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.Bosch Software Innovations GmbH.

#include <gmock/gmock.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

#include "rclcpp/rclcpp.hpp"

#include "rosbag2_test_common/subscription_manager.hpp"

#include "rosbag2_transport/player.hpp"
#include "rosbag2_transport/qos.hpp"

#include "test_msgs/msg/arrays.hpp"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/message_fixtures.hpp"

#include "rosbag2_play_test_fixture.hpp"
#include "rosbag2_transport_test_fixture.hpp"

#include "mock_player.hpp"

using namespace ::testing;  // NOLINT
using namespace rosbag2_transport;  // NOLINT
using namespace std::chrono_literals;  // NOLINT
using namespace rosbag2_test_common;  // NOLINT

constexpr int kIntValue{32};

constexpr float kFloat1Value{40.};
constexpr float kFloat2Value{2.};
constexpr float kFloat3Value{0.};

constexpr bool kBool1Value{false};
constexpr bool kBool2Value{true};
constexpr bool kBool3Value{false};

#define EVAL_REPLAYED_PRIMITIVES(replayed_primitives) \
  EXPECT_THAT( \
    replayed_primitives, \
    Each(Pointee(Field(&test_msgs::msg::BasicTypes::int32_value, kIntValue))))

#define EVAL_REPLAYED_FLOAT_ARRAY_PRIMITIVES(replayed_float_array_primitive) \
  EXPECT_THAT( \
    replayed_float_array_primitive, \
    Each( \
      Pointee( \
        Field( \
          &test_msgs::msg::Arrays::float32_values, \
          ElementsAre(kFloat1Value, kFloat2Value, kFloat3Value)))))

#define EVAL_REPLAYED_BOOL_ARRAY_PRIMITIVES(replayed_bool_array_primitive) \
  EXPECT_THAT( \
    replayed_bool_array_primitive, \
    Each( \
      Pointee( \
        Field( \
          &test_msgs::msg::Arrays::bool_values, \
          ElementsAre(kBool1Value, kBool2Value, kBool3Value)))))

class RosBag2PlayUntilTestFixture : public RosBag2PlayTestFixture
{
public:
  static constexpr const char * kTopic1Name_{"topic1"};
  static constexpr const char * kTopic2Name_{"topic2"};
  static constexpr const char * kTopic1_{"/topic1"};
  static constexpr const char * kTopic2_{"/topic2"};

  std::vector<rosbag2_storage::TopicMetadata> get_topic_types()
  {
    return {{kTopic1Name_, "test_msgs/BasicTypes", "", ""},
      {kTopic2Name_, "test_msgs/Arrays", "", ""}};
  }

  std::vector<std::shared_ptr<rosbag2_storage::SerializedBagMessage>>
  get_serialized_messages()
  {
    auto primitive_message1 = get_messages_basic_types()[0];
    primitive_message1->int32_value = kIntValue;

    auto complex_message1 = get_messages_arrays()[0];
    complex_message1->float32_values = {{kFloat1Value, kFloat2Value, kFloat3Value}};
    complex_message1->bool_values = {{kBool1Value, kBool2Value, kBool3Value}};

    // @{ Ordering matters. The mock reader implementation moves messages
    //    around without any knowledge about message chronology. It just picks
    //    the next one Make sure to keep the list in order or sort it!
    std::vector<std::shared_ptr<rosbag2_storage::SerializedBagMessage>> messages =
    {serialize_test_message(kTopic1Name_, 100, primitive_message1),
      serialize_test_message(kTopic2Name_, 120, complex_message1),
      serialize_test_message(kTopic1Name_, 200, primitive_message1),
      serialize_test_message(kTopic2Name_, 220, complex_message1),
      serialize_test_message(kTopic1Name_, 300, primitive_message1),
      serialize_test_message(kTopic2Name_, 320, complex_message1)};
    // @}
    return messages;
  }

  void InitPlayerWithPlaybackUntilAndPlay(
    int64_t playback_until_timestamp_millis,
    size_t expected_number_of_messages_on_topic1 = 3,
    size_t expected_number_of_messages_on_topic2 = 3,
    int64_t playback_duration_millis = -1)
  {
    auto topic_types = get_topic_types();
    auto messages = get_serialized_messages();

    auto prepared_mock_reader = std::make_unique<MockSequentialReader>();
    prepared_mock_reader->prepare(messages, topic_types);
    auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(prepared_mock_reader));

    sub_->add_subscription<test_msgs::msg::BasicTypes>(
      kTopic1_, expected_number_of_messages_on_topic1);
    sub_->add_subscription<test_msgs::msg::Arrays>(kTopic2_, expected_number_of_messages_on_topic2);

    play_options_.playback_until_timestamp = RCL_MS_TO_NS(playback_until_timestamp_millis);
    play_options_.playback_duration =
      rclcpp::Duration(std::chrono::milliseconds(playback_duration_millis));
    player_ = std::make_shared<MockPlayer>(std::move(reader), storage_options_, play_options_);

    // Wait for discovery to match publishers with subscribers
    ASSERT_TRUE(sub_->spin_and_wait_for_matched(player_->get_list_of_publishers(), 5s));

    auto await_received_messages = sub_->spin_subscriptions();
    ASSERT_TRUE(player_->play());
    await_received_messages.get();
  }

  std::shared_ptr<MockPlayer> player_;
};


TEST_F(RosBag2PlayUntilTestFixture, play_until_all_are_played_due_to_timestamp)
{
  InitPlayerWithPlaybackUntilAndPlay(350);

  auto replayed_test_primitives = sub_->get_received_messages<test_msgs::msg::BasicTypes>(
    kTopic1_);
  EXPECT_THAT(replayed_test_primitives, SizeIs(Eq(3u)));
  EVAL_REPLAYED_PRIMITIVES(replayed_test_primitives);

  auto replayed_test_arrays = sub_->get_received_messages<test_msgs::msg::Arrays>(
    kTopic2_);
  EXPECT_THAT(replayed_test_arrays, SizeIs(Eq(3u)));
  EVAL_REPLAYED_BOOL_ARRAY_PRIMITIVES(replayed_test_arrays);
  EVAL_REPLAYED_FLOAT_ARRAY_PRIMITIVES(replayed_test_arrays);
}

TEST_F(RosBag2PlayUntilTestFixture, play_until_none_are_played_due_to_timestamp)
{
  auto primitive_message1 = get_messages_basic_types()[0];
  auto primitive_message2 = get_messages_basic_types()[0];
  primitive_message1->int32_value = 1;
  primitive_message2->int32_value = 2;

  auto topic_types = std::vector<rosbag2_storage::TopicMetadata>{
    {kTopic1Name_, "test_msgs/BasicTypes", "", ""}};

  std::vector<std::shared_ptr<rosbag2_storage::SerializedBagMessage>> messages =
  {
    serialize_test_message(kTopic1Name_, 50, primitive_message1),
    serialize_test_message(kTopic1Name_, 100, primitive_message2),
  };

  auto prepared_mock_reader = std::make_unique<MockSequentialReader>();
  prepared_mock_reader->prepare(messages, topic_types);
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(prepared_mock_reader));

  // Expect to receive no messages.
  sub_->add_subscription<test_msgs::msg::BasicTypes>(kTopic1_, 0u);
  play_options_.playback_until_timestamp = RCL_MS_TO_NS(50) - 1;

  std::shared_ptr<MockPlayer> player_ = std::make_shared<MockPlayer>(
    std::move(reader), storage_options_, play_options_);

  // Wait for discovery to match publishers with subscribers
  ASSERT_TRUE(sub_->spin_and_wait_for_matched(player_->get_list_of_publishers(), 5s));

  auto await_received_messages = sub_->spin_subscriptions();
  ASSERT_TRUE(player_->play());

  // Playing one more time with play_next() to save time and count messages. Note
  // that none of the following play() and play_next() functions will make any of
  // the messages to be played.
  player_->pause();
  auto player_future = std::async(std::launch::async, [&player_]() -> void {player_->play();});

  EXPECT_FALSE(player_->play_next());
  player_->resume();
  player_future.get();
  await_received_messages.get();
  auto replayed_topic1 = sub_->get_received_messages<test_msgs::msg::BasicTypes>(kTopic1_);
  EXPECT_THAT(replayed_topic1, SizeIs(0));
}

TEST_F(RosBag2PlayUntilTestFixture, play_until_less_than_the_total_duration)
{
  auto primitive_message1 = get_messages_basic_types()[0];
  auto primitive_message2 = get_messages_basic_types()[0];
  primitive_message1->int32_value = 1;
  primitive_message2->int32_value = 2;

  auto topic_types = std::vector<rosbag2_storage::TopicMetadata>{
    {kTopic1Name_, "test_msgs/BasicTypes", "", ""}};

  std::vector<std::shared_ptr<rosbag2_storage::SerializedBagMessage>> messages =
  {
    serialize_test_message(kTopic1Name_, 10, primitive_message1),
    serialize_test_message(kTopic1Name_, 50, primitive_message2),
  };

  auto prepared_mock_reader = std::make_unique<MockSequentialReader>();
  prepared_mock_reader->prepare(messages, topic_types);
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(prepared_mock_reader));

  // Expect to receive 1 message from play() and 1 message from play_next in second round.
  sub_->add_subscription<test_msgs::msg::BasicTypes>(kTopic1_, 2u);
  play_options_.playback_until_timestamp = RCL_MS_TO_NS(50) - 1;

  std::shared_ptr<MockPlayer> player_ = std::make_shared<MockPlayer>(
    std::move(reader), storage_options_, play_options_);

  // Wait for discovery to match publishers with subscribers
  ASSERT_TRUE(sub_->spin_and_wait_for_matched(player_->get_list_of_publishers(), 5s));

  auto await_received_messages = sub_->spin_subscriptions();
  ASSERT_TRUE(player_->play());

  // Playing one more time with play_next() to save time and count messages.
  player_->pause();
  auto player_future = std::async(std::launch::async, [&player_]() -> void {player_->play();});

  ASSERT_TRUE(player_->play_next());
  ASSERT_FALSE(player_->play_next());
  player_->resume();
  player_future.get();
  await_received_messages.get();
  auto replayed_topic1 = sub_->get_received_messages<test_msgs::msg::BasicTypes>(kTopic1_);
  EXPECT_THAT(replayed_topic1, SizeIs(2));
  EXPECT_EQ(replayed_topic1[0]->int32_value, 1);
  EXPECT_EQ(replayed_topic1[1]->int32_value, 1);
}

TEST_F(
  RosBag2PlayUntilTestFixture,
  play_until_intermediate_duration_recorded_messages_with_filtered_topics)
{
  // Filter allows /topic2, blocks /topic1
  play_options_.topics_to_filter = {"topic2"};
  InitPlayerWithPlaybackUntilAndPlay(270, 0, 2);

  auto replayed_test_primitives =
    sub_->get_received_messages<test_msgs::msg::BasicTypes>("/topic1");
  // No messages are allowed to have arrived
  EXPECT_THAT(replayed_test_primitives, SizeIs(Eq(0u)));

  auto replayed_test_arrays = sub_->get_received_messages<test_msgs::msg::Arrays>("/topic2");
  // Some messages should have arrived.
  EXPECT_THAT(replayed_test_arrays, SizeIs(Eq(2u)));
  EVAL_REPLAYED_BOOL_ARRAY_PRIMITIVES(replayed_test_arrays);
  EVAL_REPLAYED_FLOAT_ARRAY_PRIMITIVES(replayed_test_arrays);
}

TEST_F(RosBag2PlayUntilTestFixture, play_should_return_false_when_interrupted)
{
  auto topic_types = std::vector<rosbag2_storage::TopicMetadata>{
    {kTopic1Name_, "test_msgs/BasicTypes", "", ""}};

  auto primitive_message = get_messages_basic_types()[0];
  primitive_message->int32_value = kIntValue;
  std::vector<std::shared_ptr<rosbag2_storage::SerializedBagMessage>> messages =
  {
    serialize_test_message(kTopic1Name_, 50, primitive_message),
    serialize_test_message(kTopic1Name_, 100, primitive_message),
  };

  auto prepared_mock_reader = std::make_unique<MockSequentialReader>();
  prepared_mock_reader->prepare(messages, topic_types);
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(prepared_mock_reader));

  // Let the player only reproduce one message.
  sub_->add_subscription<test_msgs::msg::BasicTypes>(kTopic1_, 1);
  play_options_.playback_until_timestamp = RCL_MS_TO_NS(75);

  std::shared_ptr<MockPlayer> player_ = std::make_shared<MockPlayer>(
    std::move(reader), storage_options_, play_options_);

  // Wait for discovery to match publishers with subscribers
  ASSERT_TRUE(sub_->spin_and_wait_for_matched(player_->get_list_of_publishers(), 5s));

  auto await_received_messages = sub_->spin_subscriptions();
  player_->pause();
  auto player_future = std::async(std::launch::async, [player_]() {return player_->play();});
  player_->wait_for_playback_to_start();
  ASSERT_TRUE(player_->is_paused());
  ASSERT_FALSE(player_->play());

  player_->resume();
  player_future.get();
  await_received_messages.get();
  auto replayed_topic1 = sub_->get_received_messages<test_msgs::msg::BasicTypes>(kTopic1_);
  EXPECT_THAT(replayed_topic1, SizeIs(1));
}

TEST_F(RosBag2PlayUntilTestFixture, play_until_overrides_playback_duration)
{
  InitPlayerWithPlaybackUntilAndPlay(
    150 /* playback_until_timestamp_millis */, 1 /* num messages topic 1 */,
    1 /* num messages topic 2 */, 50 /* playback_duration_millis */);

  auto replayed_test_primitives = sub_->get_received_messages<test_msgs::msg::BasicTypes>(
    kTopic1_);
  EXPECT_THAT(replayed_test_primitives, SizeIs(Eq(1u)));
  EVAL_REPLAYED_PRIMITIVES(replayed_test_primitives);

  auto replayed_test_arrays = sub_->get_received_messages<test_msgs::msg::Arrays>(
    kTopic2_);
  EXPECT_THAT(replayed_test_arrays, SizeIs(Eq(1u)));
  EVAL_REPLAYED_BOOL_ARRAY_PRIMITIVES(replayed_test_arrays);
  EVAL_REPLAYED_FLOAT_ARRAY_PRIMITIVES(replayed_test_arrays);
}

TEST_F(RosBag2PlayUntilTestFixture, playback_duration_overrides_play_until)
{
  InitPlayerWithPlaybackUntilAndPlay(
    50 /* playback_until_timestamp_millis */, 1 /* num messages topic 1 */,
    1 /* num messages topic 2 */, 150 /* playback_duration_millis */);

  auto replayed_test_primitives = sub_->get_received_messages<test_msgs::msg::BasicTypes>(
    kTopic1_);
  EXPECT_THAT(replayed_test_primitives, SizeIs(Eq(1u)));
  EVAL_REPLAYED_PRIMITIVES(replayed_test_primitives);

  auto replayed_test_arrays = sub_->get_received_messages<test_msgs::msg::Arrays>(
    kTopic2_);
  EXPECT_THAT(replayed_test_arrays, SizeIs(Eq(1u)));
  EVAL_REPLAYED_BOOL_ARRAY_PRIMITIVES(replayed_test_arrays);
  EVAL_REPLAYED_FLOAT_ARRAY_PRIMITIVES(replayed_test_arrays);
}

TEST_F(RosBag2PlayUntilTestFixture, play_until_is_equal_to_the_total_duration)
{
  auto primitive_message1 = get_messages_basic_types()[0];
  auto primitive_message2 = get_messages_basic_types()[0];
  primitive_message1->int32_value = 1;
  primitive_message2->int32_value = 2;

  auto topic_types = std::vector<rosbag2_storage::TopicMetadata>{
    {kTopic1Name_, "test_msgs/BasicTypes", "", ""}};

  std::vector<std::shared_ptr<rosbag2_storage::SerializedBagMessage>> messages =
  {
    serialize_test_message(kTopic1Name_, 10, primitive_message1),
    serialize_test_message(kTopic1Name_, 50, primitive_message2),
  };

  auto prepared_mock_reader = std::make_unique<MockSequentialReader>();
  prepared_mock_reader->prepare(messages, topic_types);
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(prepared_mock_reader));

  // Expect to receive 1 message from play() and 2 messages from play_next in second round
  sub_->add_subscription<test_msgs::msg::BasicTypes>(kTopic1_, messages.size());
  play_options_.playback_until_timestamp = RCL_MS_TO_NS(50);

  std::shared_ptr<MockPlayer> player_ = std::make_shared<MockPlayer>(
    std::move(reader), storage_options_, play_options_);

  // Wait for discovery to match publishers with subscribers
  ASSERT_TRUE(sub_->spin_and_wait_for_matched(player_->get_list_of_publishers(), 5s));

  auto await_received_messages = sub_->spin_subscriptions();
  ASSERT_TRUE(player_->play());

  await_received_messages.get();
  auto replayed_topic1 = sub_->get_received_messages<test_msgs::msg::BasicTypes>(kTopic1_);
  EXPECT_THAT(replayed_topic1, SizeIs(messages.size()));
  EXPECT_EQ(replayed_topic1[0]->int32_value, 1);
  EXPECT_EQ(replayed_topic1[1]->int32_value, 2);
}
