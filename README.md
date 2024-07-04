# luaprofiler
一个luaprofiler，通过tcp将数据传给python(你也可以传给其他)

网上的luaprofiler

要么很复杂如：https://github.com/young40/LuaProfiler
也没有写线程，导致我导入cocos中的时候，复杂的项目异常卡顿

要么很简单如云风的：https://github.com/cloudwu/luaprofiler
信息太少

没有很复杂，基于云风的,扩充了一些，并写了线程，而且通过tcp将信息传给其他程序
方便我后续写工具
