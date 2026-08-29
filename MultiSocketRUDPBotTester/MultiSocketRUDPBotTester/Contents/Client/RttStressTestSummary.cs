namespace MultiSocketRUDPBotTester.Contents.Client
{
    public sealed class RttStressTestConfiguration
    {
        public required int ClientCount { get; init; }
        public required int WarmupSeconds { get; init; }
        public required int MeasureSeconds { get; init; }
        public required int TimeoutMs { get; init; }
        public int RetransmissionTimeoutMs { get; init; } = 100;
        public int RetransmissionMaxCount { get; init; } = 16;
    }

    public sealed class RttStressTestSummary
    {
        public string Library { get; init; } = "MultiSocketRUDP";
        public string Mode { get; init; } = "closed_loop_one_outstanding_per_client";
        public int ApplicationPayloadBytes { get; init; }
        public int TargetClients { get; init; }
        public int ConnectedClients { get; init; }
        public int ConnectionFailures { get; init; }
        public int RuntimeDisconnects { get; init; }
        public int RetransmissionLimitDisconnects { get; init; }
        public int ServerUnresponsiveDisconnects { get; init; }
        public double AllConnectedMs { get; init; }
        public int WarmupSeconds { get; init; }
        public int MeasureSeconds { get; init; }
        public int TimeoutMs { get; init; }
        public int RetransmissionTimeoutMs { get; init; }
        public int RetransmissionMaxCount { get; init; }
        public long PingsSent { get; init; }
        public long PongsReceived { get; init; }
        public long Timeouts { get; init; }
        public double TimeoutRatePercent { get; init; }
        public long InvalidResponses { get; init; }
        public long SendFailures { get; init; }
        public double AchievedRoundTripsPerSecond { get; init; }
        public double RttAverageMs { get; init; }
        public double RttMinMs { get; init; }
        public double RttP50Ms { get; init; }
        public double RttP95Ms { get; init; }
        public double RttP99Ms { get; init; }
        public double RttP999Ms { get; init; }
        public double RttMaxMs { get; init; }
        public double ClientRpsMin { get; init; }
        public double ClientRpsP50 { get; init; }
        public double ClientRpsP95 { get; init; }
        public double ClientRpsMax { get; init; }
        public double ProcessCpuCorePercent { get; init; }
        public double ProcessCpuHostPercent { get; init; }
        public double ProcessWorkingSetMb { get; init; }
        public double ProcessPeakWorkingSetMb { get; init; }
    }
}
