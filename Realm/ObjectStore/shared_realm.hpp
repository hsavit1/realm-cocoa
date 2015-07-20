////////////////////////////////////////////////////////////////////////////
//
// Copyright 2015 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#ifndef REALM_REALM_HPP
#define REALM_REALM_HPP

#include <memory>
#include <thread>
#include <vector>
#include <mutex>
#include <set>
#include <map>
#include "object_store.hpp"
#include <realm/group.hpp>

namespace realm {
    class RealmCache;
    class Realm;
    typedef std::shared_ptr<Realm> SharedRealm;
    typedef std::weak_ptr<Realm> WeakRealm;

    class Realm
    {
      public:
        struct Config
        {
            std::string path;
            bool read_only;
            bool in_memory;
            StringData encryption_key;

            std::unique_ptr<ObjectStore::Schema> schema;
            uint64_t schema_version;
            ObjectStore::MigrationFunction migration_function;

            Config() = default;
            Config(const Config& c);
        };

        // Get a cached Realm or create a new one if no cached copies exists
        // Caching is done by path - mismatches for inMemory and readOnly Config properties
        // will raise an exception
        // If schema/schema_version is specified, update_schema is called automatically on the realm
        // and a migration is performed. If not specified, the schema version and schema are dynamically
        // read from the the existing Realm.
        static SharedRealm get_shared_realm(Config &config);

        // Updates a Realm to a given target schema/version creating tables and updating indexes as necessary
        // Uses the existing migration function on the Config, and the resulting Schema and version with updated
        // column mappings are set on the realms config upon success.
        // returns if any changes were made
        bool update_schema(ObjectStore::Schema &schema, uint64_t version);

        const Config &config() const { return m_config; }

        void begin_transaction();
        void commit_transaction();
        void cancel_transaction();
        bool is_in_transaction() { return m_in_transaction; }

        enum CreationOptions {
            None = 0,
            Update = 1 << 0,
            Promote = 2 << 0
        };
        template<typename ValueType, typename DictType>
        Row create_object(std::string class_name, DictType value, bool try_update);
        
        bool refresh();
        void set_auto_refresh(bool auto_refresh) { m_auto_refresh = auto_refresh; }
        bool auto_refresh() { return m_auto_refresh; }
        void notify();

        typedef std::shared_ptr<std::function<void(const std::string)>> NotificationFunction;
        void add_notification(NotificationFunction &notification) { m_notifications.insert(notification); }
        void remove_notification(NotificationFunction notification) { m_notifications.erase(notification); }

        void invalidate();
        bool compact();

        std::thread::id thread_id() const { return m_thread_id; }
        void verify_thread();

        const std::string RefreshRequiredNotification = "RefreshRequiredNotification";
        const std::string DidChangeNotification = "DidChangeNotification";

      private:
        Realm(Config &config);
        //Realm(const Realm& r) = delete;

        Config m_config;
        std::thread::id m_thread_id;
        bool m_in_transaction;
        bool m_auto_refresh;

        std::set<NotificationFunction> m_notifications;
        void send_local_notifications(const std::string &notification);

        typedef std::unique_ptr<std::function<void()>> ExternalNotificationFunction;
        void send_external_notifications() { if (m_external_notifier) (*m_external_notifier)(); }

        std::unique_ptr<Replication> m_replication;
        std::unique_ptr<SharedGroup> m_shared_group;
        std::unique_ptr<Group> m_read_only_group;

        Group *m_group;

    public: // FIXME private
        Group *read_group();

        ExternalNotificationFunction m_external_notifier;

        static std::mutex s_init_mutex;
        static RealmCache s_global_cache;
    };

    class RealmException : public std::exception
    {
      public:
        enum class Kind
        {
            /** Options specified in the config do not match other Realm instances opened on the same thread */
            MismatchedConfig,
            /** Thrown for any I/O related exception scenarios when a realm is opened. */
            FileAccessError,
            /** Thrown if the user does not have permission to open or create
             the specified file in the specified access mode when the realm is opened. */
            FilePermissionDenied,
            /** Thrown if no_create was specified and the file did already exist when the realm is opened. */
            FileExists,
            /** Thrown if no_create was specified and the file was not found when the realm is opened. */
            FileNotFound,
            /** Thrown if the database file is currently open in another
             process which cannot share with the current process due to an
             architecture mismatch. */
            IncompatibleLockFile,
            InvalidTransaction,
            IncorrectThread,
            /** Thrown when trying to open an unitialized Realm without a target schema or with a mismatching
             schema version **/
            InvalidSchemaVersion,
            MissingPropertyValue
        };
        RealmException(Kind kind, std::string message) : m_kind(kind), m_what(message) {}

        virtual const char *what() noexcept { return m_what.c_str(); }
        Kind kind() const { return m_kind; }
        
      private:
        Kind m_kind;
        std::string m_what;
    };

    class RealmCache
    {
      public:
        SharedRealm get_realm(const std::string &path, std::thread::id thread_id = std::this_thread::get_id());
        SharedRealm get_any_realm(const std::string &path);
        void remove(const std::string &path, std::thread::id thread_id);
        void cache_realm(SharedRealm &realm, std::thread::id thread_id = std::this_thread::get_id());

      private:
        std::map<const std::string, std::map<std::thread::id, WeakRealm>> m_cache;
        std::mutex m_mutex;
    };
}

#endif /* defined(REALM_REALM_HPP) */