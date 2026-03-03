using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester.Graph
{
    public interface INodeBuilder
    {
        bool CanBuild(NodeVisual visual);
        ActionNodeBase Build(NodeVisual visual);
    }
}