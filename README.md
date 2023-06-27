# XECS
这是一个仅C++头文件的实体组件系统库。它使用了C++17支持的std::pmr空间下的容器和memory_resource，因此请确保您能够使用支持C++17的编译器。


## Tutorial
此ECS库是一个基于数据驱动的库，要想使用它请你先了解ECS的实体组件系统架构。


### 自定义组件结构
为方便用户使用和对脚本的扩展，组件可以是任意数据结构，无需一定要求是POD类型。
```C++
struct position
{
    static X::type_id type()
    {
        return "position";
    }

    float x = 0.0f;
    float y = 0.0f;
}
struct rotation
{
    static X::type_id type()
    {
        return "rotation";
    }

    float angle = 0.0f;
}
struct scale
{
    static X::type_id type()
    {
        return "scale";
    }

    float x = 0.0f;
    float y = 0.0f;
}
```


### 自定义系统函数
为约束用户在系统中不得含有数据的要求，系统只能被定义为普通函数；
函数参数有一定的要求，X::world必须是指针类型，但可以不需要，X::entity必须是值类型，同样可以不需要，其他组件类型必须为指针类型；
component *为写入组件，const component *为读取组件，也可通过X::read<component>、X::write<component>来进行包装。
```C++
void left_move( X::world * world, X::entity entity, position * pos, X::read<rotation> rot )
{
    pos->x -= 1;
    sprintf( "%llu: angle: %f\n", entity.id(), rot->angle );
}
```


### 创建内存资源表
内存资源可以由用户给出，也可使用默认模式，默认模式则由std::pmr::get_default_resource()提供。
```C++
X::memory_resource resource;
```


### 创建调度器
调度器是有多个线程组成的池，可以共享给多个世界使用。
```C++
X::scheduler scheduler( resource );
```


### 创建世界
世界负责组织实体和组件并且负责调度系统的结构，同一时刻可以创建多个世界。
```C++
X::world world( &scheduler );
```


### 注册系统
通过世界来将需要的系统函数注册入世界中；
可通过X::all、X::any、X::none这三个过滤器来对实体进行过滤处理；
如果使用脚本扩展，可以通过自行构建X::detail::system_info，在使用world.register_system来进行注册。
```C++
world.register_system( "left_move", left_move, X::all<position，rotation>{}, X::any<scale>{} );
```


### 创建实体
通过世界创建实体，并给出所需的组件类型；
如果使用脚本扩展，可以通过自行构建std::span<X::detail::component_info>，在使用world.create_entity、world.attach_entity、world.detach_entity来进行创建、附加和剥离。
```C++
X::entity e = world.create_entity<position, rotation>();
world.attach_entity<scale>( e );
```


### 启动世界
通过世界的startup函数来启动世界，并创建对应的实体和构建系统拓扑图。
```C++
world.startup();
```


### 更新世界
此时可以通过世界的update函数来使世界运行起来。
```C++
world.update();
```


### 清理世界
通过世界的clearup函数来清理不需要的世界。
```C++
world.clearup();
```


## 注意事项
通过世界对系统和实体进行的所有操作都会压入到任务队列中，在startup、update的最后、clearup中进行处理，所以这些任务可能是有延时的。
