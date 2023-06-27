#pragma once

#include <set>
#include <map>
#include <span>
#include <tuple>
#include <queue>
#include <vector>
#include <string>
#include <thread>
#include <future>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <memory_resource>
#include <condition_variable>

#ifndef X_ARCHETYPE_CHUNK_SIZE
#define X_ARCHETYPE_CHUNK_SIZE (16384)
#endif// !X_ARCHETYPE_CHUNK_SIZE

namespace X
{
	class world;
	class entity;
	class scheduler;
	class memory_resource;
	template< typename T > class ptr;
	template< typename T > class read;
	template< typename T > class write;
	template< typename ... T > struct all;
	template< typename ... T > struct any;
	template< typename ... T > struct none;
	template< typename T > class graph;

	using type_id = std::string;
	using hash_id = std::uint64_t;
	using command = std::function<void()>;
	using job_task = std::function<void()>;
	using type_list = std::pmr::vector< type_id >;
	using destructable = std::function<void( std::uint8_t * )>;
	using constructable = std::function<void( std::uint8_t * )>;
	using copy_assignable = std::function<void( std::uint8_t *, std::uint8_t * )>;
	using serializable = std::function<void( std::ostream & , std::uint8_t * )>;
	using deserializable = std::function<void( std::istream &, std::uint8_t * )>;

	using system_callback = std::function<void( std::span< std::uint8_t * > )>;
	
	class memory_resource
	{
	public:
		std::pmr::memory_resource * graph_resource = std::pmr::get_default_resource();
		std::pmr::memory_resource * frame_resource = std::pmr::get_default_resource();
		std::pmr::memory_resource * entity_resource = std::pmr::get_default_resource();
		std::pmr::memory_resource * system_resource = std::pmr::get_default_resource();
		std::pmr::memory_resource * command_resource = std::pmr::get_default_resource();
		std::pmr::memory_resource * archetype_resource = std::pmr::get_default_resource();
		std::pmr::memory_resource * scheduler_resource = std::pmr::get_default_resource();
		std::pmr::memory_resource * callstack_resource = std::pmr::get_default_resource();
	};

	template< typename T > class ptr
	{
	public:
		using value_type = T;
		using pointer_type = T *;

	public:
		static X::type_id type()
		{
			return T::type();
		}

	public:
		ptr() = default;

		ptr( ptr && val )
		{
			swap( val );
		}

		ptr( const ptr & val )
			:_ptr( val._ptr )
		{

		}

		ptr( pointer_type val )
			:_ptr( val )
		{
		}

		ptr & operator=( ptr && val )
		{
			swap( val );
			return *this;
		}

		ptr & operator=( const ptr & val )
		{
			_ptr = val._ptr;
			return *this;
		}

		ptr & operator=( pointer_type val )
		{
			_ptr = val;
			return *this;
		}

	public:
		operator bool() const
		{
			return _ptr != nullptr;
		}

		pointer_type operator->() const
		{
			return _ptr;
		}

	public:
		pointer_type get() const
		{
			return _ptr;
		}

	public:
		void swap( ptr & val )
		{
			std::swap( _ptr, val._ptr );
		}

	private:
		pointer_type _ptr = nullptr;
	};
	template< typename T > class read : public ptr< const T >
	{
	public:
		using ptr< const T >::ptr;
	};
	template< typename T > struct is_read : public std::false_type {}; template< typename T > struct is_read< read< T > > : public std::true_type {}; template< typename T > constexpr const bool is_read_v = is_read< T >::value;
	template< typename T > class write : public ptr< T >
	{
	public:
		using ptr< T >::ptr;
	};
	template< typename T > struct is_write : public std::false_type {}; template< typename T > struct is_write< write< T > > : public std::true_type {}; template< typename T > constexpr const bool is_write_v = is_write< T >::value;

	template<> struct all<> { using this_type = std::identity; };
	template< typename T, typename ... Args > struct all< T, Args... > : private all< Args... > { using this_type = T; using base_type = all< Args... >; };
	template< typename ... T > struct is_all : public std::false_type {}; template< typename ... T > struct is_all< all< T... > > : public std::true_type {}; template< typename T > constexpr const bool is_all_v = is_all<T>::value;
	template<> struct any<> { using this_type = std::identity; };
	template< typename T, typename ... Args > struct any< T, Args... > : private any< Args... > { using this_type = T; using base_type = any< Args... >; };
	template< typename ... T > struct is_any : public std::false_type {}; template< typename ... T > struct is_any< any< T... > > : public std::true_type {}; template< typename T > constexpr const bool is_any_v = is_any<T>::value;
	template<> struct none<> { using this_type = std::identity; };
	template< typename T, typename ... Args > struct none< T, Args... > : private none< Args... > { using this_type = T; using base_type = none< Args... >; };
	template< typename ... T > struct is_none : public std::false_type {}; template< typename ... T > struct is_none< none< T... > > : public std::true_type {}; template< typename T > constexpr const bool is_none_v = is_none<T>::value;
	template< typename ... T > struct is_filter : public std::false_type {};
	template< typename ... T > struct is_filter< all< T... > > : public std::true_type {};
	template< typename ... T > struct is_filter< any< T... > > : public std::true_type {};
	template< typename ... T > struct is_filter< none< T... > > : public std::true_type {};
	template< typename T > constexpr const bool is_filter_v = is_filter<T>::value;

	namespace detail
	{
		static constexpr const std::size_t npos = static_cast<std::size_t>( -1 );

		struct address
		{
			bool operator<( const address & addr ) const
			{
				return chunk_index < addr.chunk_index ? true : chunk_index == addr.chunk_index ? address_index < addr.address_index : false;
			}

			std::size_t chunk_index = npos;
			std::size_t address_index = npos;
		};

		struct entity_info
		{
			hash_id type;
			address address;
			std::uint64_t id;
		};

		struct system_info
		{
			system_info( X::memory_resource * resource )
				: alls( resource->system_resource ), anys( resource->system_resource ), nones( resource->system_resource ), reads( resource->system_resource ), writes( resource->system_resource ), arguments( resource->system_resource )
			{

			}

			bool enable = true;
			type_id type;
			type_list alls;
			type_list anys;
			type_list nones;
			type_list reads;
			type_list writes;
			type_list arguments;
			system_callback function;
		};

		struct component_info
		{
			type_id type;
			std::size_t size;
			destructable destruct;
			constructable construct;
			copy_assignable copyassgin;
			serializable serialize;
			deserializable deserialize;
		};

		struct archetype_info
		{
		public:
			class chunk
			{
			public:
				chunk( X::memory_resource * resource, std::span<component_info> components )
					: _bits( resource->archetype_resource )
				{
					std::fill( _data, _data + X_ARCHETYPE_CHUNK_SIZE, 0 );

					std::size_t total_size = 0;
					for ( const auto & it : components )
						total_size += it.size;

					_count = 0;
					_components = components;

					_bits.resize( X_ARCHETYPE_CHUNK_SIZE / total_size );
				}

			public:
				bool full() const
				{
					return _count == _bits.size();
				}

				bool empty() const
				{
					return _count == 0;
				}

				std::size_t size() const
				{
					return _count;
				}

				std::size_t capacity() const
				{
					return _bits.size();
				}

			public:
				std::uint8_t * address( std::size_t index, std::size_t component_info )
				{
					return component_address( component_info ) + ( index * _components[component_info].size );
				}

				std::size_t alloc()
				{
					if ( !full() )
					{
						auto it = std::find( _bits.begin(), _bits.end(), false );
						if ( it != _bits.end() )
						{
							*it = true;
							_count++;

							return it - _bits.begin();
						}
					}

					return npos;
				}

				void free( std::size_t index )
				{
					if ( !empty() )
					{
						_count--;
						_bits[index] = false;
					}
				}

				template< typename F > void foreach( X::detail::address addr, F & func )
				{
					for ( size_t i = 0; i < _bits.size(); i++ )
					{
						if ( _bits[i] )
						{
							addr.address_index = i;
							func( addr );
						}
					}
				}

			private:
				std::uint8_t * component_address( std::size_t component_info )
				{
					std::uint8_t * addr = _data;
					for ( size_t i = 0; i < component_info; i++ )
					{
						addr += _components[i].size * _bits.size();
					}
					return addr;
				}

			private:
				std::size_t _count;
				std::pmr::vector<bool> _bits;
				std::span<component_info> _components;

				std::uint8_t _data[X_ARCHETYPE_CHUNK_SIZE];
			};

		public:
			archetype_info( X::memory_resource * resource, std::span<component_info> components )
				: _resource( resource ), _chunks( resource->archetype_resource ), _components(components.begin(), components.end(), resource->archetype_resource), _entitys(resource->archetype_resource)
			{
			}

			~archetype_info()
			{
				for ( auto it : _chunks )
				{
					it->~chunk();
					_resource->archetype_resource->deallocate( it, sizeof( chunk ) );
				}
			}

		public:
			address alloc( entity_info * entity )
			{
				address addr;

				for ( size_t i = 0; i < _chunks.size(); i++ )
				{
					if ( !_chunks[i]->full() )
					{
						addr.chunk_index = i;
						addr.address_index = _chunks[i]->alloc();
						break;
					}
				}

				if ( addr.address_index == npos )
				{
					chunk * c = new ( _resource->archetype_resource->allocate( sizeof( chunk ) ) ) chunk( _resource, _components );
					addr.chunk_index = _chunks.size();
					addr.address_index = c->alloc();
					_chunks.push_back( c );
				}

				for ( size_t i = 0; i < _components.size(); i++ )
				{
					std::uint8_t * ptr = _chunks[addr.chunk_index]->address( addr.address_index, i );
					_components[i].construct( ptr );
				}

				_entitys.insert( { addr, entity } );

				return addr;
			}

			void free( address addr )
			{
				for ( size_t i = 0; i < _components.size(); i++ )
				{
					_components[i].destruct( component_address<std::uint8_t>( addr, i ) );
				}

				_chunks[addr.chunk_index]->free( addr.address_index );

				_entitys.erase( _entitys.find( addr ) );
			}

			template< typename T > T * component_address( address addr, std::size_t component )
			{
				return (T *)( _chunks[addr.chunk_index]->address( addr.address_index, component ) );
			}

			template< typename F > void foreach( F func )
			{
				for ( size_t i = 0; i < _chunks.size(); i++ )
				{
					if ( !_chunks[i]->empty() )
					{
						address addr;
						addr.chunk_index = i;

						_chunks[i]->foreach( addr, func );
					}
				}
			}

		public:
			hash_id hash_code() const
			{
				std::hash<type_id> hash;
				std::uint64_t _Val = 14695981039346656037ULL;
				for ( const auto & it : _components )
					_Val ^= hash( it.type );
				return _Val;
			}

			std::span<const component_info> components() const
			{
				return { _components };
			}

			entity_info * find_entity( const address & addr ) const
			{
				auto it = _entitys.find( addr );

				return it != _entitys.end() ? it->second : nullptr;
			}

		private:
			X::memory_resource * _resource;
			std::pmr::vector<chunk *> _chunks;
			std::pmr::vector<component_info> _components;
			std::pmr::map<address, entity_info *> _entitys;
		};

		template< typename T > struct type
		{
			using value_type = std::remove_cvref_t< std::remove_pointer_t< T > >;

			static X::type_id of()
			{
				return value_type::type();
			}
		};

		template< typename T > struct cast;
		template< typename T > struct cast< T * >
		{
			static T * of( std::uint8_t * ptr )
			{
				return (T *)ptr;
			}
		};
		template< typename T > struct cast< read< T > >
		{
			static read< T > of( std::uint8_t * ptr )
			{
				return (T *)ptr;
			}
		};
		template< typename T > struct cast< write< T > >
		{
			static write< T > of( std::uint8_t * ptr )
			{
				return (T *)ptr;
			}
		};

		template< typename F > struct __function_traits;
		template< typename R, typename ...As > struct __function_traits_base
		{
			using function_type = std::function< R( As... ) >;
			using result_type = R;
			using argument_types = std::tuple< As... >;
		};
		template< typename R, typename ...As > struct __function_traits< R( * )( As... ) > : public __function_traits_base< R, As... > {};
		template< typename R, typename C, typename ...As > struct __function_traits< R( C:: * )( As... ) > : public __function_traits_base< R, As... > {};
		template< typename R, typename C, typename ...As > struct __function_traits< R( C:: * )( As... ) const > : public __function_traits_base< R, As... > {};
		template< typename F > struct __function_traits : public __function_traits< decltype( &F::operator() ) > {};
		template< typename F > struct function_traits : public __function_traits< std::decay_t< F > > {};

		template <typename T> struct is_save
		{
			static constexpr bool check( ... ) { return false; };
			template< typename U, void( U:: * )( std::ostream & ) = &U::save > static constexpr bool check( U * ) { return true; };
		};
		template <typename T> static constexpr bool is_save_v = is_save<T>::check( static_cast<T *>( nullptr ) );
		template <typename T> struct is_load
		{
			static constexpr bool check( ... ) { return false; };
			template< typename U, void( U:: * )( std::istream & ) = &U::load > static constexpr bool check( U * ) { return true; };
		};
		template <typename T> static constexpr bool is_load_v = is_load<T>::check( static_cast<T *>( nullptr ) );

		inline hash_id hash_code( std::span< component_info > components )
		{
			std::hash<type_id> hash;
			std::uint64_t _Val = 14695981039346656037ULL;
			for ( const auto & it : components )
				_Val ^= hash( it.type );
			return _Val;
		}
	}

	template< typename T > class graph
	{
	public:
		using edge_id = std::size_t;
		using vertex_id = std::size_t;

		using vertex_type = T;
		using edge_type = std::pair< vertex_id, vertex_id >;

	public:
		static constexpr vertex_id npos = vertex_id( -1 );

	public:
		graph( X::memory_resource * resource )
			: _edges( resource->graph_resource ), _vertices( resource->graph_resource )
		{

		}

	public:
		vertex_id add_vertex( vertex_type v )
		{
			auto id = _vertices.size();
			_vertices.push_back( v );
			return id;
		}

		edge_id add_edge( vertex_id left, vertex_id right )
		{
			auto id = _edges.size();
			_edges.push_back( { left, right } );
			return id;
		}

		vertex_id edge_source( edge_id id ) const
		{
			return _edges[id].first;
		}

		vertex_id edge_target( edge_id id ) const
		{
			return _edges[id].second;
		}

	public:
		vertex_id root_vertex() const
		{
			for ( size_t i = 0; i < _vertices.size(); ++i )
			{
				auto sz = std::count_if( _edges.begin(), _edges.end(), [i] ( const auto & val ) { return val.second == i; } );
				if ( sz == 0 )
					return i;
			}

			return npos;
		}

		vertex_type & vertex( vertex_id id )
		{
			return _vertices[id];
		}

		const vertex_type & vertex( vertex_id id ) const
		{
			return _vertices[id];
		}

		vertex_id vertex_index( const vertex_type & v ) const
		{
			auto it = std::find( _vertices.begin(), _vertices.end(), v );

			return it == _vertices.end() ? npos : it - _vertices.begin();
		}

		vertex_id vertex_source( vertex_id id ) const
		{
			auto it = std::find_if( _edges.begin(), _edges.end(), [&id] ( const auto & val ) { return val.second == id; } );
			if ( it != _edges.end() )
				return it->first;

			return npos;
		}

		template< typename C > void vertex_targets( C & container, vertex_id id ) const
		{
			for ( const auto & it : _edges )
			{
				if ( it.first == id )
				{
					container.push_back( it.second );
				}
			}
		}

		void clear()
		{
			_edges.clear();
			_vertices.clear();
		}

	public:
		auto vertices_begin()
		{
			return _vertices.begin();
		}
		auto vertices_end()
		{
			return _vertices.end();
		}
		auto vertices_begin() const
		{
			return _vertices.begin();
		}
		auto vertices_end() const
		{
			return _vertices.end();
		}
		auto edges_begin()
		{
			return _edges.begin();
		}
		auto edges_begin() const
		{
			return _edges.begin();
		}
		auto edges_end()
		{
			return _edges.end();
		}
		auto edges_end() const
		{
			return _edges.end();
		}

	public:
		std::size_t edges_size() const
		{
			return _edges.size();
		}
		std::size_t vertices_size() const
		{
			return _vertices.size();
		}
		const std::pmr::vector< edge_type > & edges() const
		{
			return _edges;
		}
		const std::pmr::vector< vertex_type > & vertices() const
		{
			return _vertices;
		}

	private:
		std::pmr::vector< edge_type > _edges;
		std::pmr::vector< vertex_type > _vertices;
	};

	class scheduler
	{
	public:
		scheduler( const X::memory_resource & resource, size_t thread_count = std::thread::hardware_concurrency() * 2 )
			: _resource( resource ), _tasks( std::pmr::polymorphic_allocator< X::job_task >( resource.scheduler_resource ) ), _threads( resource.scheduler_resource )
		{
			for ( size_t i = 0; i < thread_count; i++ )
			{
				_threads.emplace_back( std::thread( [this] ()
				{
					job_task task;

					while ( !_exit )
					{
						if ( _tasks.empty() )
						{
							std::unique_lock< std::mutex > lock( _mutex );
							_cond.wait( lock );
						}

						if ( !_tasks.empty() )
						{
							std::unique_lock< std::mutex > lock( _queue );

							if ( !_tasks.empty() )
							{
								task = std::move( _tasks.front() );
								_tasks.pop();
							}
						}

						if ( task )
						{
							task();
							task = nullptr;
						}
					}
				} ) );
			}
		}

		~scheduler()
		{
			_exit = true;
			_cond.notify_all();
			for ( auto & it : _threads )
			{
				if ( it.joinable() )
					it.join();
			}
		}

	public:
		std::future<void> push_task( job_task task )
		{
			std::shared_ptr< std::promise<void> > promise = std::allocate_shared< std::promise<void> >( std::pmr::polymorphic_allocator< std::promise<void> >( _resource.frame_resource ) );

			auto future = promise->get_future();
			{
				std::unique_lock< std::mutex > lock( _queue );

				_tasks.push( [promise = std::move( promise ), task = std::move( task )] () { if ( task ) task(); promise->set_value(); } );
			}
			_cond.notify_one();

			return future;
		}

	public:
		X::memory_resource * resource()
		{
			return &_resource;
		}

	private:
		bool _exit = false;
		std::mutex _mutex, _queue;
		X::memory_resource _resource;
		std::condition_variable _cond;
		std::pmr::vector< std::thread > _threads;
		std::queue< job_task, std::pmr::deque< job_task > > _tasks;
	};

	class entity
	{
	public:
		static X::type_id type()
		{
			return "entity";
		}

	public:
		entity()
			:_id( detail::npos )
		{

		}

		entity( std::uint64_t id )
			:_id( id )
		{

		}

		entity( const entity & val )
			: _id( val._id )
		{

		}

		entity & operator=( std::uint64_t id )
		{
			_id = id;
			return *this;
		}

		entity & operator=( const entity & val )
		{
			_id = val._id;
			return *this;
		}

	public:
		operator std::uint64_t() const
		{
			return _id;
		}

	public:
		std::uint64_t id() const
		{
			return _id;
		}

	private:
		std::uint64_t _id;
	};
	template<> struct detail::cast< X::entity >
	{
		static X::entity of( std::uint8_t * ptr )
		{
			return reinterpret_cast<X::detail::entity_info *>( ptr )->id;
		}
	};
	template< typename T > using is_entity = std::is_same<T, X::entity >;
	template< typename T > static constexpr bool is_entity_v = is_entity< T >::value;

	class world
	{
	public:
		static X::type_id type()
		{
			return "world";
		}

	public:
		world( X::scheduler * scheduler )
			: _scheduler( scheduler ), _graph( scheduler->resource() ), _free_entity( scheduler->resource()->entity_resource ), _entitys( scheduler->resource()->entity_resource ), _systems( scheduler->resource()->system_resource ), _archetypes( scheduler->resource()->archetype_resource ), _commands( std::pmr::polymorphic_allocator< std::pair< command_type, command > >( scheduler->resource()->command_resource ) )
		{
		}

		~world() = default;

	public:
		template< typename F, typename ... Filter > void register_system( const type_id & type, F && f, Filter && ... filters )
		{
			static_assert( std::is_void_v< typename X::detail::function_traits< F >::result_type >, "System functions cannot have return values" );

			X::detail::system_info info( _scheduler->resource() );
			{
				info.type = type;
				unpack_filters< Filter... >( info );
				unpack_arguments< typename X::detail::function_traits< F >::argument_types >::of( info );
				info.function = [f = std::move( f )]( std::span< std::uint8_t * > ptrs )
				{
					std::apply( f, trans_arguments< typename X::detail::function_traits< F >::argument_types >::of( ptrs ) );
				};
			}
			register_system( type, std::move( info ) );
		}

		inline void register_system( const type_id & type, X::detail::system_info && sys )
		{
			_systems.insert( { type, sys } );

			push_command( command_type::BUILD_SYSTEM, [this] () { build(); } );
		}

		inline void enable_system( const type_id & type )
		{
			push_command( command_type::BUILD_SYSTEM, [this, type] ()
			{
				auto it = _systems.find( type );
				if ( it != _systems.end() )
				{
					it->second.enable = true;
					build();
				}
			} );
		}

		inline void disable_system( const type_id & type )
		{
			push_command( command_type::BUILD_SYSTEM, [this, type] ()
			{
				auto it = _systems.find( type );
				if ( it != _systems.end() )
				{
					it->second.enable = false;
					build();
				}
			} );
		}

		inline void unregister_system( const type_id & type )
		{
			push_command( command_type::BUILD_SYSTEM, [this, type] ()
			{
				auto it = _systems.find( type );
				if ( it != _systems.end() )
				{
					_systems.erase( it );
					build();
				}
			} );
		}

	public:
		template< typename ... T > entity create_entity()
		{
			static_assert( sizeof...( T ) != 0, "The component cannot be empty" );
			
			std::pmr::vector<X::detail::component_info> infos( _scheduler->resource()->callstack_resource );
			unpack_components<T...>( infos );
			return create_entity( infos );
		}

		template< typename ... T > void attach_entity( entity id )
		{
			std::pmr::vector<X::detail::component_info> infos( _scheduler->resource()->callstack_resource );
			unpack_components<T...>( infos );
			attach_entity( id, infos );
		}

		template< typename ... T > void detach_entity( entity id )
		{
			std::pmr::vector<X::detail::component_info> infos( _scheduler->resource()->callstack_resource );
			unpack_components<T...>( infos );
			detach_entity( id, infos );
		}

		inline entity create_entity( std::span< X::detail::component_info > infos )
		{
			std::uint64_t id = 0;

			if ( !_free_entity.empty() )
			{
				id = _free_entity.front();
				_free_entity.pop_front();
			}
			else
			{
				id = _entitys.size();
				_entitys.push_back( {} );
			}

			auto archetype = X::detail::hash_code( infos );
			auto it = _archetypes.find( archetype );
			if ( it == _archetypes.end() )
			{
				it = _archetypes.insert( { archetype, { _scheduler->resource(), { infos } } } ).first;
			}

			_entitys[id].id = id;
			_entitys[id].type = it->first;

			push_command( command_type::CREATE_ENTITY, [this, id, archetype] ()
			{
				auto it = _archetypes.find( archetype );
				if ( it != _archetypes.end() )
				{
					_entitys[id].address = it->second.alloc( &_entitys[id] );
				}
			} );

			return id;
		}

		inline void attach_entity( entity id, std::span< X::detail::component_info > infos )
		{
			auto old_archetype = _entitys[id].type;
			auto old_archetype_it = _archetypes.find( old_archetype );

			std::pmr::vector<X::detail::component_info> new_infos( _scheduler->resource()->callstack_resource );
			new_infos.assign( old_archetype_it->second.components().begin(), old_archetype_it->second.components().end() );
			new_infos.insert( new_infos.end(), infos.begin(), infos.end() );

			auto new_archetype = X::detail::hash_code( new_infos );

			if ( old_archetype != new_archetype )
			{
				auto new_archetype_it = _archetypes.find( new_archetype );
				if ( new_archetype_it == _archetypes.end() )
				{
					new_archetype_it = _archetypes.insert( { new_archetype, { _scheduler->resource(), { new_infos } } } ).first;
				}

				_entitys[id].type = new_archetype_it->first;

				push_command( command_type::ATTACH_ENTITY, [this, id, old_archetype, new_archetype] ()
				{
					auto old_it = _archetypes.find( old_archetype );
					auto new_it = _archetypes.find( new_archetype );
					auto old_infos = old_it->second.components();
					auto new_infos = new_it->second.components();
					auto old_address = _entitys[id].address;
					auto new_address = new_it->second.alloc( &_entitys[id] );

					for ( size_t i = 0; i < old_infos.size(); i++ )
					{
						size_t j = std::find_if( new_infos.begin(), new_infos.end(), [&] ( const auto & val ) { return val.type == old_infos[i].type; } ) - new_infos.begin();

						auto old_ptr = old_it->second.component_address<std::uint8_t>( old_address, i );
						auto new_ptr = new_it->second.component_address<std::uint8_t>( old_address, j );

						old_infos[i].copyassgin( new_ptr, old_ptr );
					}

					old_it->second.free( old_address );
					_entitys[id].address = new_address;
				} );
			}
		}

		inline void detach_entity( entity id, std::span< X::detail::component_info > infos )
		{
			auto old_archetype = _entitys[id].type;
			auto old_archetype_it = _archetypes.find( old_archetype );

			std::pmr::vector<X::detail::component_info> new_infos( _scheduler->resource()->callstack_resource );
			new_infos.assign( old_archetype_it->second.components().begin(), old_archetype_it->second.components().end() );
			new_infos.erase( std::remove_if( new_infos.begin(), new_infos.end(), [&infos] ( const auto & left ) { return std::find_if( infos.begin(), infos.end(), [&left] ( const auto & right ) { return left.type == right.type; } ) != infos.end(); } ), new_infos.end() );

			auto new_archetype = X::detail::hash_code( new_infos );

			if ( old_archetype != new_archetype )
			{
				auto new_archetype_it = _archetypes.find( new_archetype );
				if ( new_archetype_it == _archetypes.end() )
				{
					new_archetype_it = _archetypes.insert( { new_archetype, { _scheduler->resource(), { new_infos } } } ).first;
				}

				_entitys[id].type = new_archetype_it->first;

				push_command( command_type::DETACH_ENTITY, [this, id, old_archetype, new_archetype] ()
				{
					auto old_it = _archetypes.find( old_archetype );
					auto new_it = _archetypes.find( new_archetype );
					auto old_infos = old_it->second.components();
					auto new_infos = new_it->second.components();
					auto old_address = _entitys[id].address;
					auto new_address = new_it->second.alloc( &_entitys[id] );

					for ( size_t i = 0; i < new_infos.size(); i++ )
					{
						size_t j = std::find_if( old_infos.begin(), old_infos.end(), [&] ( const auto & val ) { return val.type == new_infos[i].type; } ) - old_infos.begin();

						auto new_ptr = new_it->second.component_address<std::uint8_t>( new_address, i );
						auto old_ptr = old_it->second.component_address<std::uint8_t>( old_address, j );

						new_infos[i].copyassgin( new_ptr, old_ptr );
					}

					old_it->second.free( old_address );
					_entitys[id].address = new_address;
				} );
			}
		}

		inline void destroy_entity( entity id )
		{
			push_command( command_type::DESTROY_ENTITY, [this, id] ()
			{
				auto it = _archetypes.find( _entitys[id].type );
				if ( it != _archetypes.end() )
				{
					it->second.free( _entitys[id].address );

					_entitys[id].id = detail::npos;
					_entitys[id].type = detail::npos;
					_entitys[id].address = {};
				}

				_free_entity.push_back( id );
			} );
		}

	public:
		inline void startup()
		{
			exec_command();
		}
		
		inline void update()
		{
			_frame++;

			execute();

			exec_command();
		}

		inline void clearup()
		{
			exec_command();

			_frame = 0;
			_build_frame = -1;

			_graph.clear();
			_entitys.clear();
			_systems.clear();
			_archetypes.clear();
			_free_entity.clear();
		}

	public:
		scheduler * scheduler() const
		{
			return _scheduler;
		}

	private:
		template< typename T > struct trans_arguments;
		template< typename ... T > struct trans_arguments< std::tuple< T... > >
		{
		public:
			static std::tuple< T... > of( std::span< std::uint8_t * > ptrs )
			{
				std::tuple< T... > t;
				pack< 0, std::tuple< T... >, T... >::of( t, ptrs );
				return t;
			}

		private:
			template< std::size_t N, typename E, typename ... U > struct pack;

			template< std::size_t N, typename E, typename U > struct pack< N, E, U >
			{
				static void of( E & t, std::span< std::uint8_t * > & p )
				{
					if constexpr ( N < std::tuple_size_v< E > )
					{
						std::get<N>( t ) = X::detail::cast< U >::of( p[N] );
					}
				}
			};

			template< std::size_t N, typename E, typename U, typename ... As > struct pack< N, E, U, As... >
			{
				static void of( E & t, std::span< std::uint8_t * > & p )
				{
					pack< N, E, U >::of( t, p );

					pack< N + 1, E, As... >::of( t, p );
				}
			};
		};

		template< typename T > struct unpack_arguments;
		template< typename ... T > struct unpack_arguments< std::tuple< T... > >
		{
		public:
			static void of( X::detail::system_info & container )
			{
				( unpack< T >( container ), ... );
			}

		private:
			template< typename U > static void unpack( X::detail::system_info & container )
			{
				using component_type = std::remove_cvref_t< U >;

				if constexpr ( is_read_v< component_type > )
				{
					container.reads.push_back( X::detail::type< component_type >::of() );
				}
				else if constexpr ( is_write_v< component_type > )
				{
					container.writes.push_back( X::detail::type< component_type >::of() );
				}
				else if constexpr ( std::is_const_v< std::remove_pointer_t< U > > )
				{
					container.reads.push_back( X::detail::type< component_type >::of() );
				}
				else if constexpr ( std::is_pointer_v< component_type > && !std::is_same_v< component_type, X::world > )
				{
					container.writes.push_back( X::detail::type< component_type >::of() );
				}
				else if constexpr ( !is_entity_v< component_type > )
				{
					static_assert( std::is_pointer_v< U >, "The system function argument must be either a component pointer or X::world pointer, X::entity, X::read<>, X::write<>, X::ahead<>" );
				}

				container.arguments.push_back( X::detail::type< component_type >::of() );
			}
		};

	private:
		template< typename T > void unpack_all_filter( X::detail::system_info & container )
		{
			if constexpr ( !std::is_same_v< typename T::this_type, std::identity > )
			{
				container.alls.push_back( T::this_type::type() );
				unpack_all_filter< typename T::base_type >( container );
			}
		}

		template< typename T > void unpack_any_filter( X::detail::system_info & container )
		{
			if constexpr ( !std::is_same_v< typename T::this_type, std::identity > )
			{
				container.anys.push_back( T::this_type::type() );
				unpack_any_filter< typename T::base_type >( container );
			}
		}

		template< typename T > void unpack_none_filter( X::detail::system_info & container )
		{
			if constexpr ( !std::is_same_v< typename T::this_type, std::identity > )
			{
				container.nones.push_back( T::this_type::type() );
				unpack_none_filter< typename T::base_type >( container );
			}
		}

		template< typename T > void unpack_filter( X::detail::system_info & container )
		{
			if constexpr ( X::is_all_v< T > )
			{
				unpack_all_filter< T >( container );
			}
			else if constexpr ( X::is_any_v< T > )
			{
				unpack_any_filter< T >( container );
			}
			else if constexpr ( X::is_none_v< T > )
			{
				unpack_none_filter< T >( container );
			}
		}

		template< typename ... T > void unpack_filters( X::detail::system_info & container )
		{
			( unpack_filter< T >( container ), ... );
		}

	private:
		template< typename T > void unpack_component( std::pmr::vector<X::detail::component_info> & container )
		{
			X::detail::component_info info;
			info.type = T::type();
			info.size = sizeof( T );
			info.destruct = [] ( std::uint8_t * ptr ) { ( (T *)( ptr ) )->~T(); };
			info.construct = [] ( std::uint8_t * ptr ) { new ( ptr ) T(); };
			info.copyassgin = [] ( std::uint8_t * ptr, std::uint8_t * other )
			{
				if constexpr ( std::is_move_assignable_v< T > )
					( (T *)( ptr ) )->operator=( std::move( *( (T *)( other ) ) ) );
				else
					( (T *)( ptr ) )->operator=( *( (T *)( other ) ) );
			};
			info.serialize = [] ( std::ostream & stream, std::uint8_t * ptr )
			{
				if constexpr ( X::detail::is_save_v< T > )
					( (T *)( ptr ) )->save( stream );
			};
			info.deserialize = [] ( std::istream & stream, std::uint8_t * ptr )
			{
				if constexpr ( X::detail::is_load_v< T > )
					( (T *)( ptr ) )->load( stream );
			};
			container.emplace_back( std::move( info ) );
		}

		template< typename ... T > void unpack_components( std::pmr::vector<X::detail::component_info> & container )
		{
			( unpack_component< T >( container ), ... );
		}

	private:
		enum command_type
		{
			CREATE_ENTITY = 0,
			ATTACH_ENTITY = 1,
			DETACH_ENTITY = 2,
			DESTROY_ENTITY = 3,
			BUILD_SYSTEM = 4,
		};

		struct command_less
		{
			using first_argument_type = std::pair< command_type, command >;
			using second_argument_type = std::pair< command_type, command >;
			using result_type = bool;

			constexpr result_type operator()( const first_argument_type & _Left, const second_argument_type & _Right ) const
			{
				return _Left.first > _Right.first;
			}
		};

		inline void exec_command()
		{
			std::unique_lock< std::mutex > lock( _mutex );

			while ( !_commands.empty() )
			{
				_commands.top().second();
				_commands.pop();
			}
		}

		inline void push_command( command_type type, command cmd )
		{
			std::unique_lock< std::mutex > lock( _mutex );

			_commands.push( { type, cmd } );
		}

	private:
		inline void build()
		{
			if ( _build_frame != _frame )
			{
				_build_frame = _frame;

				_graph.clear();
				for ( auto & it : _systems )
					_graph.add_vertex( &it.second );

				const auto & vertices = _graph.vertices();
				std::pmr::vector< std::pmr::vector< size_t > > indegree( _scheduler->resource()->callstack_resource );
				indegree.resize( vertices.size() );

				for ( size_t i = 0; i < vertices.size(); i++ )
				{
					const auto & reads = vertices[i]->reads;

					for ( size_t j = 0; j < vertices.size(); j++ )
					{
						if ( i != j )
						{
							const auto & write = vertices[j]->writes;

							if ( std::any_of( write.begin(), write.end(), [&] ( const auto & val ) { return std::find( reads.begin(), reads.end(), val ) != reads.end(); } ) )
							{
								indegree[i].push_back( j );
							}
						}
					}
				}

				std::pmr::set< const std::pmr::vector< size_t > * > exclude( _scheduler->resource()->callstack_resource );
				for ( auto it = std::find_if( indegree.begin(), indegree.end(), [] ( const auto & val ) { return val.size() == 0; } ); it != indegree.end(); it = std::find_if( indegree.begin(), indegree.end(), [&exclude] ( const auto & val ) { return val.size() == 0 && exclude.find( &val ) == exclude.end(); } ) )
				{
					exclude.insert( &*it );

					auto left_id = it - indegree.begin();

					for ( auto it1 = indegree.begin(); it1 != indegree.end(); ++it1 )
					{
						auto it2 = std::find( it1->begin(), it1->end(), left_id );
						if ( it2 != it1->end() )
						{
							auto right_id = it1 - indegree.begin();

							it1->erase( it2 );

							_graph.add_edge( left_id, right_id );
						}
					}
				}

				for ( const auto & it : indegree )
				{
					if ( !it.empty() )
					{
						_graph.clear();
						std::cout << "Graph topology sort discovery ring" << std::endl;
						return;
					}
				}
			}
		}

		inline void execute()
		{
			std::pmr::vector< std::future< void > > futures( _scheduler->resource()->callstack_resource );

			auto root = _graph.root_vertex();
			if ( root != _graph.npos )
			{
				std::pmr::deque< X::graph< X::detail::system_info * >::vertex_id > current( _scheduler->resource()->callstack_resource );
				std::pmr::deque< X::graph< X::detail::system_info * >::vertex_id > nextlayer( _scheduler->resource()->callstack_resource );

				current.push_back( root );

				do
				{
					if ( !nextlayer.empty() )
					{
						current.insert( current.end(), nextlayer.begin(), nextlayer.end() );
						nextlayer.clear();
					}

					while ( !current.empty() )
					{
						futures.emplace_back( std::move( post( _graph.vertex( current.front() ) ) ) );

						_graph.vertex_targets( nextlayer, current.front() );

						current.pop_front();
					}

					for ( auto & it : futures )
					{
						it.wait();
					}
					futures.clear();

				} while ( !nextlayer.empty() );
			}
		}

		inline std::future< void > post( X::detail::system_info * info )
		{
			std::pmr::vector< hash_id > archetypes( _scheduler->resource()->frame_resource );

			for ( const auto & it : _archetypes )
			{
				auto components = it.second.components();

				X::type_list arguments( _scheduler->resource()->callstack_resource );
				arguments.assign( info->arguments.begin(), info->arguments.end() );
				arguments.erase( std::remove( arguments.begin(), arguments.end(), "world" ), arguments.end() );
				arguments.erase( std::remove( arguments.begin(), arguments.end(), "entity" ), arguments.end() );

				if ( !arguments.empty() && !std::all_of( arguments.begin(), arguments.end(), [&] ( const auto & type ) { return std::find_if( components.begin(), components.end(), [&] ( const auto & component ) { return component.type == type; } ) != components.end(); } ) )
					continue;

				if ( !info->nones.empty() && !std::none_of( info->nones.begin(), info->nones.end(), [&] ( const auto & type ) { return std::find_if( components.begin(), components.end(), [&] ( const auto & component ) { return component.type == type; } ) != components.end(); } ) )
					continue;

				if ( !info->alls.empty() && !std::all_of( info->alls.begin(), info->alls.end(), [&] ( const auto & type ) { return std::find_if( components.begin(), components.end(), [&] ( const auto & component ) { return component.type == type; } ) != components.end(); } ) )
					continue;

				if ( !info->anys.empty() && !std::any_of( info->anys.begin(), info->anys.end(), [&] ( const auto & type ) { return std::find_if( components.begin(), components.end(), [&] ( const auto & component ) { return component.type == type; } ) != components.end(); } ) )
					continue;

				archetypes.push_back( it.first );
			}

			return _scheduler->push_task( [this, info, archetypes = std::move( archetypes )] () mutable
			{
				for ( auto id : archetypes )
				{
					auto it = _archetypes.find( id );
					if ( it != _archetypes.end() )
					{
						auto & type = it->second;
						std::pmr::vector< std::uint8_t * > arguments( _scheduler->resource()->callstack_resource );
						type.foreach( [this, info, &type, &arguments] ( X::detail::address addr )
						{
							arguments.clear();
							auto components = type.components();
							for ( const auto & it : info->arguments )
							{
								if ( it == "world" )
								{
									arguments.push_back( (std::uint8_t *)this );
								}
								else if ( it == "entity" )
								{
									arguments.push_back( (std::uint8_t *)type.find_entity( addr ) );
								}
								else
								{
									arguments.push_back( type.component_address< std::uint8_t >( addr, std::find_if( components.begin(), components.end(), [&it] ( const auto & val ) { return val.type == it; } ) - components.begin() ) );
								}
							}
							info->function( { arguments } );
						} );
					}
				}
			} );
		}

	private:
		std::uint64_t _frame = 0;
		std::uint64_t _build_frame = -1;

		X::scheduler * _scheduler = nullptr;
		X::graph< X::detail::system_info * > _graph;

		std::pmr::deque< std::size_t > _free_entity;
		std::pmr::vector< detail::entity_info > _entitys;
		std::pmr::unordered_map< type_id, detail::system_info > _systems;
		std::pmr::unordered_map< hash_id, detail::archetype_info > _archetypes;

		std::mutex _mutex;
		std::priority_queue< std::pair< command_type, command >, std::pmr::deque< std::pair< command_type, command > >, command_less >  _commands;
	};
}
