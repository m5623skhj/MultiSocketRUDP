using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester.UI
{
    public class NodeConfigPanelRegistry(Action<NodeVisual, int> createDynamicPorts)
    {
        private readonly List<INodeConfigPanel> panels =
        [
            new SendPacketConfigPanel(),
            new DelayConfigPanel(),
            new RandomDelayConfigPanel(),
            new LogConfigPanel(),
            new DisconnectConfigPanel(),
            new WaitForPacketConfigPanel(),
            new SetVariableConfigPanel(),
            new GetVariableConfigPanel(),
            new LoopConfigPanel(),
            new RepeatTimerConfigPanel(),
            new AssertConfigPanel(),
            new RetryConfigPanel(),
            new PacketParserConfigPanel(),
            new RandomChoiceConfigPanel(createDynamicPorts),
            new ConditionalConfigPanel()
        ];

        public INodeConfigPanel? Find(NodeVisual node) =>
            panels.FirstOrDefault(p => p.CanConfigure(node));
    }
}