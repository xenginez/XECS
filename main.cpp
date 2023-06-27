#include "XECS.h"

#include <iostream>
#include <iomanip>

#define componet_n( N ) \
struct componet##N \
{ \
	static X::type_id type() \
	{ \
		return "componet"#N; \
	} \
	int i = N; \
};

componet_n( 1 );
componet_n( 2 );
componet_n( 3 );
componet_n( 4 );
componet_n( 5 );
componet_n( 6 );
componet_n( 7 );
componet_n( 8 );
componet_n( 9 );

void componet_system1( componet1 * c1, X::write< componet2 > c2, componet3 * c3, X::entity entity, X::world * world )
{
	printf( "0x%032llu componet_system1( componet%d, componet%d, %llu, 0x%p)\n", std::hash<std::thread::id>()( std::this_thread::get_id() ), c1->i, c2->i, entity.id(), world );
}
void componet_system2( const componet1 * c1, X::read< componet2 > c2, X::entity entity, X::world * world )
{
	printf( "0x%032llu componet_system2( componet%d, componet%d, %llu, 0x%p)\n", std::hash<std::thread::id>()( std::this_thread::get_id() ), c1->i, c2->i, entity.id(), world );
}
void componet_system3( const componet1 * c1, X::read< componet3 > c3, X::entity entity, X::world * world )
{
	printf( "0x%032llu componet_system3( componet%d, componet%d, %llu, 0x%p)\n", std::hash<std::thread::id>()( std::this_thread::get_id() ), c1->i, c3->i, entity.id(), world );
}

int main()
{
	X::memory_resource resource;

	X::scheduler scheduler( resource );
	X::world world( &scheduler );

	printf( "world: %p\n", &world );

	world.register_system( "componet_system1", componet_system1 );
	world.register_system( "componet_system2", componet_system2 );
	world.register_system( "componet_system3", componet_system3 );

	auto e = world.create_entity<componet1, componet2, componet3, componet4, componet5, componet6, componet7>();
	auto e1 = world.create_entity<componet3>();

	world.attach_entity<componet8>( e );
	world.attach_entity<componet1>( e1 );
	world.attach_entity<componet9>( e );
	world.attach_entity<componet2>( e1 );

	world.startup();

	for ( size_t i = 0; i < 3; i++ )
	{
		world.update();
		printf( "\n" );
	}
	world.clearup();

	return 0;
}
