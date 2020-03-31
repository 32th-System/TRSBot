#include <stddef.h>
#include <stdio.h>

#include <chrono>
#include <string>
#include <thread>

#include <curl/curl.h>

#include <aegis.hpp>

using json = nlohmann::json;
char *TWURL_LOC;
aegis::channel *discord_channel;
aegis::core *bot_global;
aegis::guild *main_guild = NULL;
int64_t tweet_role_id;
int64_t auto_role_id;
int64_t main_guild_id;
int64_t tweet_channel_id;

void tweet(const char *message) {
	if(strlen(message) < 279) {
		char command[400];
		sprintf(command, "twurl -d 'status=%s' /1.1/statuses/update.json > tweet.json", message);
		system(command);
		FILE *tweet = fopen("tweet.json", "rb");
		if(tweet) {
			fseek(tweet, 0, SEEK_END);
			size_t tweet_size = ftell(tweet);
			fseek(tweet, 0, SEEK_SET);
			char *tweet_str = (char*)malloc(tweet_size + 1);
			fread(tweet_str, 1, tweet_size, tweet);
			fclose(tweet);
			tweet_str[tweet_size] = '\x00';
			json tweet_json = json::parse(tweet_str);
			
			std::string tweet_url("https://twitter.com/thrpyshowcase/status/");
			try {
				tweet_url.append(tweet_json["id_str"].get<std::string>().c_str());
				while(!discord_channel) {
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
				discord_channel->create_message(tweet_url);
			} catch(...) {
				free(tweet_str);
				return;
			}
			free(tweet_str);
			return;
		}

	}
}

void msg_callback(aegis::gateway::events::message_create msg_obj) {
	const char *msg_content = msg_obj.msg.get_content().c_str();
	if(strncmp(msg_content, "!tweet", strlen("!tweet")) == 0) {
		if(msg_obj.msg.get_guild().member_has_role(msg_obj.msg.get_author_id(), aegis::snowflake(tweet_role_id))) {
			msg_content += strlen("!tweet") + 1;
			tweet(msg_content);
		}
	}
}

void new_member_callback(aegis::gateway::events::guild_member_add new_member) {
    aegis::guild *guild = bot_global->find_guild(main_guild_id);
    if(guild) {
		// TODO: make this work
		// I have no idea why but for some reason this always throws std::system_error
        guild->add_guild_member_role(new_member.member._user.id, auto_role_id);
    }
}

void init_after_ready(aegis::gateway::events::ready ready) {
	discord_channel = bot_global->channel_create(tweet_channel_id);
}


size_t twitch_curl_callback(char *data, size_t size, size_t nmemb, std::string *out) {
	puts("libcurl code getting called");
	out->append(data, nmemb);
	return nmemb;
}

void twitch_thread_func(const char *header, const char *url) {
	CURLcode error;
	error = curl_global_init(CURL_GLOBAL_DEFAULT);
	if(error) {
		fprintf(stderr, "cURL global initialization failed. Error: %d\n", error);
		return;
	}
	
	CURL *curl = curl_easy_init();
	if(!curl) {
		puts("Failed to initialize cURL");
		return;
	}
	
	error = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	if(error) {
		fprintf(stderr, "Failed to enabble SSL certificate hostname verification. Error: %d\n", error);
	}
	error = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	if(error) {
		fprintf(stderr, "Failed to enable SSL ceriticate verification. Error: %d\n", error);
	}
	
	struct curl_slist *request_header = NULL;
	request_header = curl_slist_append(request_header, header);
	error = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_header);
	if(error) {
		fprintf(stderr, "Failed to set HTTP header. Error: %d\n", error);
		return;
	}
	
	error = curl_easy_setopt(curl, CURLOPT_URL, url);
	if(error) {
		fprintf(stderr, "Failed to set API URL. Error: %d\n", error);
	}
	
	std::string *ret_data = new std::string;

	error = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, twitch_curl_callback);
	if(error) {
		fprintf(stderr, "Failed to set write function, error %d\n", error);
		return;
	}
	error = curl_easy_setopt(curl, CURLOPT_WRITEDATA, ret_data);
	if(error) {
		fprintf(stderr, "Failed to set write function data, error %d\n", error);
		return;
	}

	bool stream_running = false;
	char tweet_str[280];
	std::string title = "";
	json response_json;
	
	for(;;std::this_thread::sleep_for(std::chrono::seconds(30))) {
		error = curl_easy_perform(curl);
		if(error) {
			fprintf(stderr, "Error performing cURL action. Error %d\n", error);
			return;
		}
		if(*ret_data == "") {
			continue;
		}
		response_json = json::parse(*ret_data);
		try {
			title = response_json["data"][0]["title"].get<std::string>();
		} catch(...) {
			title = "";
		}
		if(stream_running == false && title != "") {
			sprintf(tweet_str, "TRS is now streaming: \"%s\" https://twitch.tv/Touhou_Replay_Showcase", title.c_str());
			tweet(tweet_str);
			stream_running = true;
		} else if(stream_running == true && title == "") {
			stream_running = false;
		}
		*ret_data = "";
		title = "";
	}	
}

int main(int argc, char **argv) {
	FILE *config_file = fopen("TRSBot.json", "rb");
	if(!config_file) {
		fprintf(stderr, "Failed to open config.json\n");
		return -1;
	}
	fseek(config_file, 0, SEEK_END);
	size_t config_size = ftell(config_file);
	fseek(config_file, 0, SEEK_SET);
	char *config = (char*)malloc(config_size + 1);
	fread(config, 1, config_size, config_file);
	config[config_size] = '\x00';
	fclose(config_file);
	json config_json = json::parse(config);
	free(config);

	std::string token = config_json["discord_token"].get<std::string>();
	main_guild_id = config_json["guild_id"].get<int64_t>();
	tweet_channel_id = config_json["tweet_channel_id"].get<int64_t>();
	tweet_role_id = config_json["tweet_role_id"].get<int64_t>();
	auto_role_id = config_json["auto_role_id"].get<int64_t>();
	std::string header = "Client-ID: ";
	header.append(config_json["twitch_client_id"].get<std::string>());
	std::string url = config_json["twitch_api_url"].get<std::string>();
	
	
	std::thread twitch_thread(twitch_thread_func, header.c_str(), url.c_str());
	
	
	aegis::core bot(aegis::create_bot_t().log_level(spdlog::level::trace).token(token));
	bot_global = &bot;
	
	bot.set_on_message_create(msg_callback);
	// bot.set_on_guild_member_add(new_member_callback);
	bot.set_on_ready(init_after_ready);
	
	bot.run();
    printf("Bot running\n");
	bot.yield();
}
